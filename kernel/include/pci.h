#ifndef LIXOS_PCI_H
#define LIXOS_PCI_H

#include <stdint.h>
#include <stddef.h>

struct pci_device {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  header_type;
    uint32_t bar[6];
};

// PCI Configuration Space I/O
uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
void pci_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value);

// Enable Bus Mastering & Memory Space
void pci_enable_bus_master(const pci_device &dev);

// Enumerate and find devices
void pci_init(void);
bool pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device *out_dev);
size_t pci_get_device_count(void);
const pci_device *pci_get_devices(void);

#endif // LIXOS_PCI_H
