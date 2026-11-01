#include "pci.h"
#include "io.h"
#include "kprint.h"

namespace {
    constexpr uint16_t PCI_CONFIG_ADDRESS = 0xCF8;
    constexpr uint16_t PCI_CONFIG_DATA    = 0xCFC;

    constexpr size_t MAX_PCI_DEVICES = 64;
    pci_device g_pci_devices[MAX_PCI_DEVICES];
    size_t g_pci_count = 0;

    uint32_t get_pci_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
        return (1U << 31) |
               (static_cast<uint32_t>(bus) << 16) |
               (static_cast<uint32_t>(slot) << 11) |
               (static_cast<uint32_t>(func) << 8) |
               (static_cast<uint32_t>(offset) & 0xFC);
    }
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, get_pci_address(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, get_pci_address(bus, slot, func, offset));
    return static_cast<uint16_t>((inl(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, get_pci_address(bus, slot, func, offset));
    return static_cast<uint8_t>((inl(PCI_CONFIG_DATA) >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, get_pci_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t old_val = pci_read32(bus, slot, func, offset & 0xFC);
    uint32_t shift = (offset & 2) * 8;
    uint32_t mask = 0xFFFF << shift;
    uint32_t new_val = (old_val & ~mask) | (static_cast<uint32_t>(value) << shift);
    pci_write32(bus, slot, func, offset & 0xFC, new_val);
}

void pci_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value) {
    uint32_t old_val = pci_read32(bus, slot, func, offset & 0xFC);
    uint32_t shift = (offset & 3) * 8;
    uint32_t mask = 0xFF << shift;
    uint32_t new_val = (old_val & ~mask) | (static_cast<uint32_t>(value) << shift);
    pci_write32(bus, slot, func, offset & 0xFC, new_val);
}

void pci_enable_bus_master(const pci_device &dev) {
    uint16_t cmd = pci_read16(dev.bus, dev.slot, dev.func, 0x04);
    // Bit 0 = I/O Space, Bit 1 = Memory Space, Bit 2 = Bus Master
    cmd |= 0x0007;
    pci_write16(dev.bus, dev.slot, dev.func, 0x04, cmd);
}

void pci_init(void) {
    g_pci_count = 0;

    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            uint16_t vendor_id = pci_read16(static_cast<uint8_t>(bus), slot, 0, 0x00);
            if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
                continue;
            }

            uint8_t header_type = pci_read8(static_cast<uint8_t>(bus), slot, 0, 0x0E);
            uint8_t func_limit = (header_type & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < func_limit; ++func) {
                uint16_t v_id = pci_read16(static_cast<uint8_t>(bus), slot, func, 0x00);
                if (v_id == 0xFFFF || v_id == 0x0000) {
                    continue;
                }

                if (g_pci_count < MAX_PCI_DEVICES) {
                    pci_device &dev = g_pci_devices[g_pci_count++];
                    dev.bus = static_cast<uint8_t>(bus);
                    dev.slot = slot;
                    dev.func = func;
                    dev.vendor_id = v_id;
                    dev.device_id = pci_read16(dev.bus, dev.slot, dev.func, 0x02);
                    dev.class_code = pci_read8(dev.bus, dev.slot, dev.func, 0x0B);
                    dev.subclass = pci_read8(dev.bus, dev.slot, dev.func, 0x0A);
                    dev.prog_if = pci_read8(dev.bus, dev.slot, dev.func, 0x09);
                    dev.header_type = pci_read8(dev.bus, dev.slot, dev.func, 0x0E);

                    // Read BARs 0..5
                    for (uint8_t b = 0; b < 6; ++b) {
                        dev.bar[b] = pci_read32(dev.bus, dev.slot, dev.func, 0x10 + (b * 4));
                    }
                }
            }
        }
    }
}

bool pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device *out_dev) {
    for (size_t i = 0; i < g_pci_count; ++i) {
        const pci_device &d = g_pci_devices[i];
        if (d.class_code == class_code && d.subclass == subclass) {
            if (prog_if == 0xFF || d.prog_if == prog_if) {
                if (out_dev) {
                    *out_dev = d;
                }
                return true;
            }
        }
    }
    return false;
}

size_t pci_get_device_count(void) {
    return g_pci_count;
}

const pci_device *pci_get_devices(void) {
    return g_pci_devices;
}
