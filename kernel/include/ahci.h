#ifndef LIXOS_AHCI_H
#define LIXOS_AHCI_H

#include <stdint.h>
#include <stddef.h>
#include "pci.h"
#include "driver.h"

constexpr uint16_t SECTOR_SIZE = 512;

// AHCI Port Information
struct ahci_port_info {
    bool     present;
    bool     is_atapi;
    uint8_t  port_num;
    uint64_t sector_count;
};

// Initialize AHCI Controller via PCI device and HHDM offset
bool ahci_init(const pci_device &pci_dev, uint64_t hhdm_offset);

// Standard Disk Read/Write API (DMA-based)
bool disk_read_sectors(uint64_t lba, uint32_t count, void *buffer);
bool disk_write_sectors(uint64_t lba, uint32_t count, const void *buffer);

// Check if disk is available
bool disk_is_available(void);

// Get the driver descriptor
const driver *ahci_get_driver(void);

#endif // LIXOS_AHCI_H
