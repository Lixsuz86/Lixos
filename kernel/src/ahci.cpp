#include "ahci.h"
#include "vmm.h"
#include "pmm.h"
#include "kprint.h"

namespace {
    constexpr uint32_t SATA_SIG_ATA   = 0x00000101;
    constexpr uint32_t SATA_SIG_ATAPI = 0xEB140101;
    constexpr uint32_t HBA_PORT_DET_PRESENT = 3;
    constexpr uint32_t HBA_PORT_IPM_ACTIVE  = 1;

    constexpr uint8_t  ATA_CMD_READ_DMA_EXT  = 0x25;
    constexpr uint8_t  ATA_CMD_WRITE_DMA_EXT = 0x35;
    constexpr uint8_t  FIS_TYPE_REG_H2D      = 0x27;

    struct hba_port {
        volatile uint32_t clb;
        volatile uint32_t clbu;
        volatile uint32_t fb;
        volatile uint32_t fbu;
        volatile uint32_t is;
        volatile uint32_t ie;
        volatile uint32_t cmd;
        volatile uint32_t rsv0;
        volatile uint32_t tfd;
        volatile uint32_t sig;
        volatile uint32_t ssts;
        volatile uint32_t sctl;
        volatile uint32_t serr;
        volatile uint32_t sact;
        volatile uint32_t ci;
        volatile uint32_t sntf;
        volatile uint32_t fbs;
        volatile uint32_t rsv1[11];
        volatile uint32_t vendor[4];
    };

    struct hba_mem {
        volatile uint32_t cap;
        volatile uint32_t ghc;
        volatile uint32_t is;
        volatile uint32_t pi;
        volatile uint32_t vs;
        volatile uint32_t ccc_ctl;
        volatile uint32_t ccc_pts;
        volatile uint32_t em_loc;
        volatile uint32_t em_ctl;
        volatile uint32_t cap2;
        volatile uint32_t bohc;
        volatile uint8_t  rsv[0xA0 - 0x2C];
        volatile uint8_t  vendor[0x100 - 0xA0];
        hba_port ports[32];
    };

    struct hba_prdt_entry {
        uint32_t dba;
        uint32_t dbau;
        uint32_t rsv0;
        uint32_t dbc:22;
        uint32_t rsv1:9;
        uint32_t i:1;
    };

    struct hba_cmd_header {
        uint8_t  cfl:5;
        uint8_t  a:1;
        uint8_t  w:1;
        uint8_t  p:1;
        uint8_t  r:1;
        uint8_t  b:1;
        uint8_t  c:1;
        uint8_t  rsv0:1;
        uint8_t  pmp:4;
        uint16_t prdtl;
        volatile uint32_t prdbc;
        uint32_t ctba;
        uint32_t ctbau;
        uint32_t rsv1[4];
    };

    struct fis_reg_h2d {
        uint8_t  fis_type;
        uint8_t  pmport:4;
        uint8_t  rsv0:3;
        uint8_t  c:1;
        uint8_t  command;
        uint8_t  featurel;
        uint8_t  lba0;
        uint8_t  lba1;
        uint8_t  lba2;
        uint8_t  device;
        uint8_t  lba3;
        uint8_t  lba4;
        uint8_t  lba5;
        uint8_t  featureh;
        uint8_t  countl;
        uint8_t  counth;
        uint8_t  icc;
        uint8_t  control;
        uint8_t  rsv1[4];
    };

    struct hba_cmd_tbl {
        uint8_t        cfis[64];
        uint8_t        acmd[16];
        uint8_t        rsv[48];
        hba_prdt_entry prdt_entry[1];
    };

    hba_mem *g_hba_mem = nullptr;
    hba_port *g_active_port = nullptr;
    uint8_t g_active_port_num = 0;
    uint64_t g_hhdm = 0;

    // DMA Allocation pointers
    uint64_t g_clb_phys = 0;
    uint64_t g_fb_phys = 0;
    uint64_t g_ctba_phys = 0;
    uint64_t g_dma_buf_phys = 0;

    void *g_clb_virt = nullptr;
    void *g_fb_virt = nullptr;
    hba_cmd_tbl *g_ctba_virt = nullptr;
    uint8_t *g_dma_buf_virt = nullptr;

    void port_stop(hba_port *port) {
        port->cmd &= ~0x0001; // Clear ST
        port->cmd &= ~0x0010; // Clear FRE

        // Wait until FR and CR clear
        while (port->cmd & (0x4000 | 0x8000)) {
            asm volatile("pause");
        }
    }

    void port_start(hba_port *port) {
        while (port->cmd & 0x8000) { // Wait for CR to clear
            asm volatile("pause");
        }
        port->cmd |= 0x0010; // Set FRE
        port->cmd |= 0x0001; // Set ST
    }

    void configure_port(hba_port *port) {
        port_stop(port);

        // Allocate physical DMA pages
        g_clb_phys = pmm_alloc_page();
        g_fb_phys = pmm_alloc_page();
        g_ctba_phys = pmm_alloc_page();
        g_dma_buf_phys = pmm_alloc_page();

        g_clb_virt = reinterpret_cast<void *>(g_clb_phys + g_hhdm);
        g_fb_virt = reinterpret_cast<void *>(g_fb_phys + g_hhdm);
        g_ctba_virt = reinterpret_cast<hba_cmd_tbl *>(g_ctba_phys + g_hhdm);
        g_dma_buf_virt = reinterpret_cast<uint8_t *>(g_dma_buf_phys + g_hhdm);

        // Zero out DMA regions
        for (size_t i = 0; i < PAGE_SIZE; ++i) {
            static_cast<uint8_t *>(g_clb_virt)[i] = 0;
            static_cast<uint8_t *>(g_fb_virt)[i] = 0;
            static_cast<uint8_t *>(static_cast<void *>(g_ctba_virt))[i] = 0;
            g_dma_buf_virt[i] = 0;
        }

        port->clb = static_cast<uint32_t>(g_clb_phys);
        port->clbu = static_cast<uint32_t>(g_clb_phys >> 32);
        port->fb = static_cast<uint32_t>(g_fb_phys);
        port->fbu = static_cast<uint32_t>(g_fb_phys >> 32);

        // Configure command header 0
        hba_cmd_header *cmd_header = reinterpret_cast<hba_cmd_header *>(g_clb_virt);
        cmd_header->ctba = static_cast<uint32_t>(g_ctba_phys);
        cmd_header->ctbau = static_cast<uint32_t>(g_ctba_phys >> 32);
        cmd_header->prdtl = 1;

        port->serr = 0xFFFFFFFF; // Clear error register
        port->is = 0xFFFFFFFF;   // Clear interrupt status

        port_start(port);
    }

    bool driver_init_wrapper(void) {
        return disk_is_available();
    }

    size_t driver_read_wrapper(void *buffer, size_t count) {
        uint32_t sectors = static_cast<uint32_t>(count / SECTOR_SIZE);
        if (sectors == 0 && count > 0) sectors = 1;
        return disk_read_sectors(0, sectors, buffer) ? count : 0;
    }

    size_t driver_write_wrapper(const void *buffer, size_t count) {
        uint32_t sectors = static_cast<uint32_t>(count / SECTOR_SIZE);
        if (sectors == 0 && count > 0) sectors = 1;
        return disk_write_sectors(0, sectors, buffer) ? count : 0;
    }

    driver g_ahci_drv_instance = {
        "AHCI SATA Disk Driver",
        driver_type::STORAGE,
        driver_init_wrapper,
        driver_read_wrapper,
        driver_write_wrapper
    };
}

bool ahci_init(const pci_device &pci_dev, uint64_t hhdm_offset) {
    g_hhdm = hhdm_offset;
    g_active_port = nullptr;

    // Enable PCI Bus Master and Memory Space
    pci_enable_bus_master(pci_dev);

    // BAR5 is the AHCI Base Address Register (ABAR)
    uint64_t abar_phys = pci_dev.bar[5] & ~0xFFF;
    if (abar_phys == 0) {
        kprintf("[FAIL] AHCI: ABAR (BAR5) address is null!\n");
        return false;
    }

    // Map ABAR into kernel master page table
    uint64_t abar_virt = abar_phys + hhdm_offset;
    uint64_t pml4 = vmm_get_kernel_pml4();
    vmm_map_page(pml4, abar_virt, abar_phys,
                 vmm_flags::PRESENT | vmm_flags::WRITABLE | vmm_flags::NO_CACHE);

    g_hba_mem = reinterpret_cast<hba_mem *>(abar_virt);

    // Enable AHCI Mode (GHC.AE = 1)
    g_hba_mem->ghc |= (1U << 31);

    // Scan implemented ports
    uint32_t pi = g_hba_mem->pi;
    for (uint8_t i = 0; i < 32; ++i) {
        if (pi & (1U << i)) {
            hba_port *port = &g_hba_mem->ports[i];
            uint32_t ssts = port->ssts;
            uint8_t ipm = (ssts >> 8) & 0x0F;
            uint8_t det = ssts & 0x0F;

            if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
                if (port->sig == SATA_SIG_ATA) {
                    g_active_port = port;
                    g_active_port_num = i;
                    configure_port(port);
                    kprintf("[ OK ] AHCI: SATA Drive Detected and Configured on Port %u\n", i);
                    return true;
                }
            }
        }
    }

    kprintf("[WARN] AHCI: Controller found but no active SATA drive connected!\n");
    return false;
}

bool disk_is_available(void) {
    return (g_active_port != nullptr);
}

bool disk_read_sectors(uint64_t lba, uint32_t count, void *buffer) {
    if (!g_active_port || !buffer || count == 0) {
        return false;
    }

    uint8_t *dest = static_cast<uint8_t *>(buffer);

    for (uint32_t chunk_idx = 0; chunk_idx < count; ++chunk_idx) {
        uint64_t cur_lba = lba + chunk_idx;

        g_active_port->is = 0xFFFFFFFF; // Clear pending interrupts

        hba_cmd_header *cmd_header = reinterpret_cast<hba_cmd_header *>(g_clb_virt);
        cmd_header->cfl = sizeof(fis_reg_h2d) / sizeof(uint32_t);
        cmd_header->w = 0; // Read
        cmd_header->prdtl = 1;
        cmd_header->prdbc = 0;

        // Set PRDT entry to DMA buffer
        g_ctba_virt->prdt_entry[0].dba = static_cast<uint32_t>(g_dma_buf_phys);
        g_ctba_virt->prdt_entry[0].dbau = static_cast<uint32_t>(g_dma_buf_phys >> 32);
        g_ctba_virt->prdt_entry[0].dbc = SECTOR_SIZE - 1;
        g_ctba_virt->prdt_entry[0].i = 1;

        // Setup Command FIS (Register H2D)
        fis_reg_h2d *cmdfis = reinterpret_cast<fis_reg_h2d *>(&g_ctba_virt->cfis[0]);
        for (size_t i = 0; i < sizeof(fis_reg_h2d); ++i) {
            reinterpret_cast<uint8_t *>(cmdfis)[i] = 0;
        }

        cmdfis->fis_type = FIS_TYPE_REG_H2D;
        cmdfis->c = 1; // Command
        cmdfis->command = ATA_CMD_READ_DMA_EXT;

        cmdfis->lba0 = static_cast<uint8_t>(cur_lba);
        cmdfis->lba1 = static_cast<uint8_t>(cur_lba >> 8);
        cmdfis->lba2 = static_cast<uint8_t>(cur_lba >> 16);
        cmdfis->device = 1 << 6; // LBA mode

        cmdfis->lba3 = static_cast<uint8_t>(cur_lba >> 24);
        cmdfis->lba4 = static_cast<uint8_t>(cur_lba >> 32);
        cmdfis->lba5 = static_cast<uint8_t>(cur_lba >> 40);

        cmdfis->countl = 1;
        cmdfis->counth = 0;

        // Issue command
        g_active_port->ci = 1;

        // Wait for completion
        uint32_t spin = 0;
        while (g_active_port->ci & 1) {
            if (g_active_port->is & (1 << 30)) { // Task File Error
                kprintf("[FAIL] AHCI: Read DMA Task File Error on LBA %u\n", static_cast<unsigned int>(cur_lba));
                return false;
            }
            if (++spin > 1000000) {
                kprintf("[FAIL] AHCI: Read DMA timeout on LBA %u\n", static_cast<unsigned int>(cur_lba));
                return false;
            }
            asm volatile("pause");
        }

        // Copy from DMA bounce buffer to destination
        for (size_t i = 0; i < SECTOR_SIZE; ++i) {
            dest[chunk_idx * SECTOR_SIZE + i] = g_dma_buf_virt[i];
        }
    }

    return true;
}

bool disk_write_sectors(uint64_t lba, uint32_t count, const void *buffer) {
    if (!g_active_port || !buffer || count == 0) {
        return false;
    }

    const uint8_t *src = static_cast<const uint8_t *>(buffer);

    for (uint32_t chunk_idx = 0; chunk_idx < count; ++chunk_idx) {
        uint64_t cur_lba = lba + chunk_idx;

        // Copy source data to DMA buffer
        for (size_t i = 0; i < SECTOR_SIZE; ++i) {
            g_dma_buf_virt[i] = src[chunk_idx * SECTOR_SIZE + i];
        }

        g_active_port->is = 0xFFFFFFFF; // Clear pending interrupts

        hba_cmd_header *cmd_header = reinterpret_cast<hba_cmd_header *>(g_clb_virt);
        cmd_header->cfl = sizeof(fis_reg_h2d) / sizeof(uint32_t);
        cmd_header->w = 1; // Write
        cmd_header->prdtl = 1;
        cmd_header->prdbc = 0;

        g_ctba_virt->prdt_entry[0].dba = static_cast<uint32_t>(g_dma_buf_phys);
        g_ctba_virt->prdt_entry[0].dbau = static_cast<uint32_t>(g_dma_buf_phys >> 32);
        g_ctba_virt->prdt_entry[0].dbc = SECTOR_SIZE - 1;
        g_ctba_virt->prdt_entry[0].i = 1;

        fis_reg_h2d *cmdfis = reinterpret_cast<fis_reg_h2d *>(&g_ctba_virt->cfis[0]);
        for (size_t i = 0; i < sizeof(fis_reg_h2d); ++i) {
            reinterpret_cast<uint8_t *>(cmdfis)[i] = 0;
        }

        cmdfis->fis_type = FIS_TYPE_REG_H2D;
        cmdfis->c = 1; // Command
        cmdfis->command = ATA_CMD_WRITE_DMA_EXT;

        cmdfis->lba0 = static_cast<uint8_t>(cur_lba);
        cmdfis->lba1 = static_cast<uint8_t>(cur_lba >> 8);
        cmdfis->lba2 = static_cast<uint8_t>(cur_lba >> 16);
        cmdfis->device = 1 << 6; // LBA mode

        cmdfis->lba3 = static_cast<uint8_t>(cur_lba >> 24);
        cmdfis->lba4 = static_cast<uint8_t>(cur_lba >> 32);
        cmdfis->lba5 = static_cast<uint8_t>(cur_lba >> 40);

        cmdfis->countl = 1;
        cmdfis->counth = 0;

        // Issue command
        g_active_port->ci = 1;

        // Wait for completion
        uint32_t spin = 0;
        while (g_active_port->ci & 1) {
            if (g_active_port->is & (1 << 30)) {
                kprintf("[FAIL] AHCI: Write DMA Task File Error on LBA %u\n", static_cast<unsigned int>(cur_lba));
                return false;
            }
            if (++spin > 1000000) {
                kprintf("[FAIL] AHCI: Write DMA timeout on LBA %u\n", static_cast<unsigned int>(cur_lba));
                return false;
            }
            asm volatile("pause");
        }
    }

    return true;
}

const driver *ahci_get_driver(void) {
    return &g_ahci_drv_instance;
}
