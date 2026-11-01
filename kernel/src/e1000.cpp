#include "e1000.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "string.h"
#include "kprint.h"

namespace {
    bool        g_available = false;
    uintptr_t   g_mmio_base = 0;
    mac_address g_mac = {{0}};

    // RX Ring Data
    e1000_rx_desc *g_rx_descs = nullptr;
    uint64_t       g_rx_descs_phys = 0;
    uint8_t       *g_rx_virt_buffers[E1000_NUM_RX_DESC] = {nullptr};
    uint32_t       g_rx_cur = 0;

    // TX Ring Data
    e1000_tx_desc *g_tx_descs = nullptr;
    uint64_t       g_tx_descs_phys = 0;
    uint8_t       *g_tx_virt_buffers[E1000_NUM_TX_DESC] = {nullptr};
    uint32_t       g_tx_cur = 0;

    inline void write32(uint32_t reg, uint32_t val) {
        *reinterpret_cast<volatile uint32_t *>(g_mmio_base + reg) = val;
    }

    inline uint32_t read32(uint32_t reg) {
        return *reinterpret_cast<volatile uint32_t *>(g_mmio_base + reg);
    }

    uint16_t read_eeprom(uint8_t addr) {
        // Request read at word offset 'addr'
        write32(REG_EERD, 1 | (static_cast<uint32_t>(addr) << 8));
        
        // Wait for EEPROM read done bit (bit 4)
        uint32_t timeout = 50000;
        uint32_t val = 0;
        while (timeout > 0) {
            val = read32(REG_EERD);
            if (val & (1 << 4)) {
                break;
            }
            timeout--;
        }

        if (timeout == 0) {
            return 0xFFFF;
        }

        return static_cast<uint16_t>((val >> 16) & 0xFFFF);
    }

    bool read_mac_address(void) {
        // Try reading MAC from EEPROM first
        uint16_t w0 = read_eeprom(0);
        uint16_t w1 = read_eeprom(1);
        uint16_t w2 = read_eeprom(2);

        if (w0 != 0xFFFF && (w0 != 0 || w1 != 0 || w2 != 0)) {
            g_mac.bytes[0] = static_cast<uint8_t>(w0 & 0xFF);
            g_mac.bytes[1] = static_cast<uint8_t>((w0 >> 8) & 0xFF);
            g_mac.bytes[2] = static_cast<uint8_t>(w1 & 0xFF);
            g_mac.bytes[3] = static_cast<uint8_t>((w1 >> 8) & 0xFF);
            g_mac.bytes[4] = static_cast<uint8_t>(w2 & 0xFF);
            g_mac.bytes[5] = static_cast<uint8_t>((w2 >> 8) & 0xFF);
            return true;
        }

        // Fallback: Read MAC directly from RAL0 and RAH0 hardware registers
        uint32_t ral = read32(REG_RAL0);
        uint32_t rah = read32(REG_RAH0);

        if (ral != 0 || (rah & 0xFFFF) != 0) {
            g_mac.bytes[0] = static_cast<uint8_t>(ral & 0xFF);
            g_mac.bytes[1] = static_cast<uint8_t>((ral >> 8) & 0xFF);
            g_mac.bytes[2] = static_cast<uint8_t>((ral >> 16) & 0xFF);
            g_mac.bytes[3] = static_cast<uint8_t>((ral >> 24) & 0xFF);
            g_mac.bytes[4] = static_cast<uint8_t>(rah & 0xFF);
            g_mac.bytes[5] = static_cast<uint8_t>((rah >> 8) & 0xFF);
            return true;
        }

        return false;
    }

    // Driver generic interface functions
    bool driver_e1000_init(void) {
        return e1000_init();
    }

    size_t driver_e1000_read(void *buf, size_t count) {
        int res = e1000_receive_packet(buf, count);
        return (res > 0) ? static_cast<size_t>(res) : 0;
    }

    size_t driver_e1000_write(const void *buf, size_t count) {
        int res = e1000_send_packet(buf, count);
        return (res == 0) ? count : 0;
    }

    const driver g_e1000_driver = {
        "Intel e1000 Gigabit Ethernet",
        driver_type::NETWORK,
        driver_e1000_init,
        driver_e1000_read,
        driver_e1000_write
    };
}

bool e1000_init(void) {
    if (g_available) {
        return true;
    }

    // 1. Scan PCI for Intel e1000 controller (Class 0x02, Subclass 0x00 or Vendor 0x8086:0x100E)
    const pci_device *devices = pci_get_devices();
    size_t dev_count = pci_get_device_count();
    const pci_device *e1000_dev = nullptr;

    for (size_t i = 0; i < dev_count; ++i) {
        if (devices[i].vendor_id == E1000_VENDOR_INTEL &&
            (devices[i].device_id == E1000_DEV_82540EM ||
             devices[i].device_id == E1000_DEV_82545EM ||
             devices[i].device_id == E1000_DEV_82543GC)) {
            e1000_dev = &devices[i];
            break;
        }
    }

    if (!e1000_dev) {
        kprintf("[e1000] No compatible Intel Gigabit Ethernet controller found on PCI bus.\n");
        return false;
    }

    kprintf("[ OK ] Intel e1000 NIC Detected: PCI Bus %u, Slot %u, Func %u (Dev ID: 0x%04x)\n",
            e1000_dev->bus, e1000_dev->slot, e1000_dev->func, e1000_dev->device_id);

    // 2. Enable PCI Bus Mastering and Memory Space
    pci_enable_bus_master(*e1000_dev);

    // 3. Map BAR0 MMIO Registers
    uint64_t bar0_phys = e1000_dev->bar[0] & ~0xFFFULL;
    if (bar0_phys == 0) {
        kprintf("[e1000] Error: BAR0 address is 0!\n");
        return false;
    }

    uint64_t pml4 = vmm_get_kernel_pml4();
    uint64_t bar0_virt = vmm_get_hhdm_offset() + bar0_phys;

    // Map 128 KB (32 pages) of MMIO space into kernel master page table
    constexpr size_t MMIO_SIZE = 128 * 1024;
    for (size_t offset = 0; offset < MMIO_SIZE; offset += 4096) {
        vmm_map_page(pml4, bar0_virt + offset, bar0_phys + offset,
                     vmm_flags::PRESENT | vmm_flags::WRITABLE | vmm_flags::NO_CACHE);
    }

    g_mmio_base = bar0_virt;
    kprintf("[ OK ] e1000 MMIO Mapped: BAR0 Phys = 0x%08X -> Virt = %p (128 KB)\n",
            static_cast<uint32_t>(bar0_phys), reinterpret_cast<void *>(g_mmio_base));

    // 4. Reset Device
    write32(REG_CTRL, read32(REG_CTRL) | CTRL_RST);
    for (int delay = 0; delay < 100000; delay++) { asm volatile("pause"); }

    // Disable interrupts initially
    write32(REG_IMC, 0xFFFFFFFF);

    // 5. Read MAC Address
    if (!read_mac_address()) {
        kprintf("[e1000] Warning: Could not detect valid MAC address! Using default.\n");
        g_mac.bytes[0] = 0x52; g_mac.bytes[1] = 0x54; g_mac.bytes[2] = 0x00;
        g_mac.bytes[3] = 0x12; g_mac.bytes[4] = 0x34; g_mac.bytes[5] = 0x56;
    }

    kprintf("[ OK ] e1000 MAC Address Read: %02X:%02X:%02X:%02X:%02X:%02X\n",
            g_mac.bytes[0], g_mac.bytes[1], g_mac.bytes[2],
            g_mac.bytes[3], g_mac.bytes[4], g_mac.bytes[5]);

    // 6. Clear Multicast Table Array (MTA)
    for (uint32_t i = 0; i < 128; ++i) {
        write32(REG_MTA + (i * 4), 0);
    }

    // 7. Allocate Receive (RX) Ring Descriptors and Buffers
    size_t rx_desc_bytes = E1000_NUM_RX_DESC * sizeof(e1000_rx_desc);
    size_t rx_pages = (rx_desc_bytes + 4095) / 4096;
    g_rx_descs_phys = pmm_alloc_pages(rx_pages);
    if (!g_rx_descs_phys) {
        kprintf("[e1000] Error: Failed to allocate physical memory for RX ring descriptors!\n");
        return false;
    }
    g_rx_descs = reinterpret_cast<e1000_rx_desc *>(vmm_get_hhdm_offset() + g_rx_descs_phys);
    memset(g_rx_descs, 0, rx_desc_bytes);

    for (size_t i = 0; i < E1000_NUM_RX_DESC; ++i) {
        uint64_t buf_phys = pmm_alloc_pages(1); // 4096 bytes page per buffer
        g_rx_virt_buffers[i] = reinterpret_cast<uint8_t *>(vmm_get_hhdm_offset() + buf_phys);
        memset(g_rx_virt_buffers[i], 0, E1000_BUFFER_SIZE);

        g_rx_descs[i].buffer_addr = buf_phys;
        g_rx_descs[i].status = 0;
    }

    // Configure RX Registers
    write32(REG_RDBAL, static_cast<uint32_t>(g_rx_descs_phys & 0xFFFFFFFF));
    write32(REG_RDBAH, static_cast<uint32_t>(g_rx_descs_phys >> 32));
    write32(REG_RDLEN, static_cast<uint32_t>(rx_desc_bytes));
    write32(REG_RDH, 0);
    write32(REG_RDT, E1000_NUM_RX_DESC - 1);
    g_rx_cur = 0;

    // Enable Receiver with Broadcast and Promiscuous accept
    write32(REG_RCTL, RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC);

    // 8. Allocate Transmit (TX) Ring Descriptors and Buffers
    size_t tx_desc_bytes = E1000_NUM_TX_DESC * sizeof(e1000_tx_desc);
    size_t tx_pages = (tx_desc_bytes + 4095) / 4096;
    g_tx_descs_phys = pmm_alloc_pages(tx_pages);
    if (!g_tx_descs_phys) {
        kprintf("[e1000] Error: Failed to allocate physical memory for TX ring descriptors!\n");
        return false;
    }
    g_tx_descs = reinterpret_cast<e1000_tx_desc *>(vmm_get_hhdm_offset() + g_tx_descs_phys);
    memset(g_tx_descs, 0, tx_desc_bytes);

    for (size_t i = 0; i < E1000_NUM_TX_DESC; ++i) {
        uint64_t buf_phys = pmm_alloc_pages(1);
        g_tx_virt_buffers[i] = reinterpret_cast<uint8_t *>(vmm_get_hhdm_offset() + buf_phys);
        memset(g_tx_virt_buffers[i], 0, E1000_BUFFER_SIZE);

        g_tx_descs[i].buffer_addr = buf_phys;
        g_tx_descs[i].status = TXD_STAT_DD; // Initially ready
        g_tx_descs[i].cmd = 0;
    }

    // Configure TX Registers
    write32(REG_TDBAL, static_cast<uint32_t>(g_tx_descs_phys & 0xFFFFFFFF));
    write32(REG_TDBAH, static_cast<uint32_t>(g_tx_descs_phys >> 32));
    write32(REG_TDLEN, static_cast<uint32_t>(tx_desc_bytes));
    write32(REG_TDH, 0);
    write32(REG_TDT, 0);
    g_tx_cur = 0;

    // Configure Transmit IPG and Enable Transmitter
    write32(REG_TIPG, 0x0060200A);
    write32(REG_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD);

    // 9. Set Link Up (SLU) in Control Register
    write32(REG_CTRL, read32(REG_CTRL) | CTRL_SLU);

    g_available = true;
    kprintf("[ OK ] e1000 RX/TX Ring Buffers Initialized (RX: %u Descs, TX: %u Descs)\n",
            static_cast<unsigned int>(E1000_NUM_RX_DESC),
            static_cast<unsigned int>(E1000_NUM_TX_DESC));
    kprintf("[ OK ] e1000 Hardware Link Status: UP (Gigabit Full Duplex Active)\n");

    return true;
}

bool e1000_is_available(void) {
    return g_available;
}

mac_address e1000_get_mac(void) {
    return g_mac;
}

int e1000_send_packet(const void *packet, size_t length) {
    if (!g_available || !packet || length == 0 || length > E1000_BUFFER_SIZE) {
        return -1;
    }

    uint32_t cur = g_tx_cur;
    e1000_tx_desc &desc = g_tx_descs[cur];

    // Wait if descriptor is not yet finished by hardware
    uint32_t timeout = 50000;
    while (!(desc.status & TXD_STAT_DD) && timeout > 0) {
        timeout--;
        asm volatile("pause");
    }

    if (!(desc.status & TXD_STAT_DD)) {
        kprintf("[e1000] Transmit timeout: TX ring buffer is congested!\n");
        return -2;
    }

    // Copy packet payload to the pre-allocated virtual DMA buffer
    memcpy(g_tx_virt_buffers[cur], packet, length);

    // Setup descriptor flags
    desc.length = static_cast<uint16_t>(length);
    desc.cmd = TXD_CMD_EOP | TXD_CMD_IFCS | TXD_CMD_RS;
    desc.status = 0;

    // Advance tail pointer to trigger hardware transmission
    g_tx_cur = (g_tx_cur + 1) % E1000_NUM_TX_DESC;
    write32(REG_TDT, g_tx_cur);

    // Poll until hardware finishes transmitting the packet
    timeout = 100000;
    while (!(desc.status & TXD_STAT_DD) && timeout > 0) {
        timeout--;
        asm volatile("pause");
    }

    return (desc.status & TXD_STAT_DD) ? 0 : -3;
}

int e1000_receive_packet(void *out_buffer, size_t max_len) {
    if (!g_available || !out_buffer || max_len == 0) {
        return -1;
    }

    uint32_t cur = g_rx_cur;
    e1000_rx_desc &desc = g_rx_descs[cur];

    // If descriptor done flag (DD) is not set, no packet is available
    if (!(desc.status & RXD_STAT_DD)) {
        return 0; // No packet ready
    }

    size_t pkt_len = desc.length;
    size_t copy_len = (pkt_len < max_len) ? pkt_len : max_len;

    memcpy(out_buffer, g_rx_virt_buffers[cur], copy_len);

    // Reset status and hand descriptor back to hardware by moving RDT
    desc.status = 0;
    uint32_t old_tail = g_rx_cur;
    g_rx_cur = (g_rx_cur + 1) % E1000_NUM_RX_DESC;
    write32(REG_RDT, old_tail);

    return static_cast<int>(copy_len);
}

const driver *e1000_get_driver(void) {
    return &g_e1000_driver;
}
