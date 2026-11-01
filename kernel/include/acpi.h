#ifndef LIXOS_ACPI_H
#define LIXOS_ACPI_H

#include <stdint.h>
#include <stddef.h>
#include "driver.h"

// ACPI Standard SDT Header
struct sdt_header {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

// ACPI Information Structure
struct acpi_info {
    bool     valid_rsdp;
    bool     valid_rsdt_xsdt;
    uint8_t  revision;
    uint32_t fadt_pm1a_cnt;
    uint32_t madt_lapic_addr;
    uint32_t ioapic_addr;
    size_t   tables_found;
};

// Initialize ACPI parsing from Limine RSDP response
bool acpi_init(uint64_t rsdp_phys_or_virt, uint64_t hhdm_offset);

// Find an ACPI table by 4-character signature (e.g. "FACP", "APIC", "HPET")
sdt_header *acpi_find_table(const char *signature);

// Retrieve ACPI diagnostic information
acpi_info acpi_get_info(void);

// Perform ACPI hardware poweroff / shutdown
void acpi_shutdown(void);

// Get the driver descriptor
const driver *acpi_get_driver(void);

#endif // LIXOS_ACPI_H
