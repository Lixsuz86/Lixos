#include "acpi.h"
#include "vmm.h"
#include "io.h"
#include "kprint.h"

namespace {
    struct rsdp_descriptor {
        char     signature[8];
        uint8_t  checksum;
        char     oem_id[6];
        uint8_t  revision;
        uint32_t rsdt_address;
        uint32_t length;
        uint64_t xsdt_address;
        uint8_t  extended_checksum;
        uint8_t  reserved[3];
    } __attribute__((packed));

    struct fadt_descriptor {
        sdt_header header;
        uint32_t   firmware_ctrl;
        uint32_t   dsdt;
        uint8_t    reserved;
        uint8_t    preferred_pm_profile;
        uint16_t   sci_int;
        uint32_t   smi_cmd;
        uint8_t    acpi_enable;
        uint8_t    acpi_disable;
        uint8_t    s4bios_req;
        uint8_t    pstate_cnt;
        uint32_t   pm1a_evt_blk;
        uint32_t   pm1b_evt_blk;
        uint32_t   pm1a_cnt_blk;
        uint32_t   pm1b_cnt_blk;
        uint32_t   pm2_cnt_blk;
        uint32_t   pm_tmr_blk;
    } __attribute__((packed));

    struct madt_descriptor {
        sdt_header header;
        uint32_t   lapic_addr;
        uint32_t   flags;
    } __attribute__((packed));

    constexpr size_t MAX_TABLES = 64;
    sdt_header *g_acpi_tables[MAX_TABLES];
    size_t g_table_count = 0;

    acpi_info g_info{};
    uint64_t g_hhdm = 0;

    bool validate_checksum(const void *ptr, size_t length) {
        const uint8_t *bytes = static_cast<const uint8_t *>(ptr);
        uint8_t sum = 0;
        for (size_t i = 0; i < length; ++i) {
            sum += bytes[i];
        }
        return (sum == 0);
    }

    bool compare_sig(const char *a, const char *b, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            if (a[i] != b[i]) return false;
        }
        return true;
    }

    bool driver_init_wrapper(void) {
        return g_info.valid_rsdp && g_info.valid_rsdt_xsdt;
    }

    driver g_acpi_drv_instance = {
        "ACPI Power & Table Manager",
        driver_type::POWER,
        driver_init_wrapper,
        nullptr,
        nullptr
    };
}

bool acpi_init(uint64_t rsdp_addr, uint64_t hhdm_offset) {
    g_hhdm = hhdm_offset;
    g_table_count = 0;
    g_info = {};

    if (!rsdp_addr) {
        kprintf("[FAIL] ACPI: No RSDP address provided by bootloader!\n");
        return false;
    }

    // Convert to virtual address if physical
    const rsdp_descriptor *rsdp = reinterpret_cast<const rsdp_descriptor *>(
        (rsdp_addr >= hhdm_offset) ? rsdp_addr : (rsdp_addr + hhdm_offset)
    );

    // 1. Verify RSDP Signature
    if (!compare_sig(rsdp->signature, "RSD PTR ", 8)) {
        kprintf("[FAIL] ACPI: Invalid RSDP signature!\n");
        return false;
    }

    // 2. Verify RSDP Base Checksum (20 bytes)
    if (!validate_checksum(rsdp, 20)) {
        kprintf("[FAIL] ACPI: RSDP base checksum verification failed!\n");
        return false;
    }

    g_info.valid_rsdp = true;
    g_info.revision = rsdp->revision;

    // Check for ACPI 2.0+ XSDT
    bool use_xsdt = false;
    uint64_t sdt_phys_addr = rsdp->rsdt_address;

    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
        if (validate_checksum(rsdp, rsdp->length)) {
            use_xsdt = true;
            sdt_phys_addr = rsdp->xsdt_address;
        }
    }

    // 3. Map and Verify Root Table (XSDT or RSDT)
    sdt_header *root_sdt = reinterpret_cast<sdt_header *>(sdt_phys_addr + hhdm_offset);

    if (!validate_checksum(root_sdt, root_sdt->length)) {
        kprintf("[FAIL] ACPI: Root SDT (%s) checksum verification failed!\n",
                use_xsdt ? "XSDT" : "RSDT");
        return false;
    }

    g_info.valid_rsdt_xsdt = true;

    // 4. Parse Root Table Entries
    if (use_xsdt) {
        size_t entries = (root_sdt->length - sizeof(sdt_header)) / sizeof(uint64_t);
        const uint64_t *ptrs = reinterpret_cast<const uint64_t *>(root_sdt + 1);

        for (size_t i = 0; i < entries && g_table_count < MAX_TABLES; ++i) {
            sdt_header *tbl = reinterpret_cast<sdt_header *>(ptrs[i] + hhdm_offset);
            if (validate_checksum(tbl, tbl->length)) {
                g_acpi_tables[g_table_count++] = tbl;
            } else {
                kprintf("[WARN] ACPI: Table checksum error on table %c%c%c%c\n",
                        tbl->signature[0], tbl->signature[1], tbl->signature[2], tbl->signature[3]);
            }
        }
    } else {
        size_t entries = (root_sdt->length - sizeof(sdt_header)) / sizeof(uint32_t);
        const uint32_t *ptrs = reinterpret_cast<const uint32_t *>(root_sdt + 1);

        for (size_t i = 0; i < entries && g_table_count < MAX_TABLES; ++i) {
            sdt_header *tbl = reinterpret_cast<sdt_header *>(static_cast<uint64_t>(ptrs[i]) + hhdm_offset);
            if (validate_checksum(tbl, tbl->length)) {
                g_acpi_tables[g_table_count++] = tbl;
            } else {
                kprintf("[WARN] ACPI: Table checksum error on table %c%c%c%c\n",
                        tbl->signature[0], tbl->signature[1], tbl->signature[2], tbl->signature[3]);
            }
        }
    }

    g_info.tables_found = g_table_count;

    // 5. Inspect FADT & MADT
    sdt_header *fadt_hdr = acpi_find_table("FACP");
    if (fadt_hdr) {
        const fadt_descriptor *fadt = reinterpret_cast<const fadt_descriptor *>(fadt_hdr);
        g_info.fadt_pm1a_cnt = fadt->pm1a_cnt_blk;
    }

    sdt_header *madt_hdr = acpi_find_table("APIC");
    if (madt_hdr) {
        const madt_descriptor *madt = reinterpret_cast<const madt_descriptor *>(madt_hdr);
        g_info.madt_lapic_addr = madt->lapic_addr;

        // Parse MADT entries for I/O APIC
        const uint8_t *curr = reinterpret_cast<const uint8_t *>(madt + 1);
        const uint8_t *end = reinterpret_cast<const uint8_t *>(madt) + madt->header.length;

        while (curr < end) {
            uint8_t type = curr[0];
            uint8_t len = curr[1];
            if (len == 0) break;

            if (type == 1) { // I/O APIC
                uint32_t ioapic_addr = *reinterpret_cast<const uint32_t *>(curr + 4);
                g_info.ioapic_addr = ioapic_addr;
            }
            curr += len;
        }
    }

    return true;
}

sdt_header *acpi_find_table(const char *signature) {
    for (size_t i = 0; i < g_table_count; ++i) {
        if (compare_sig(g_acpi_tables[i]->signature, signature, 4)) {
            return g_acpi_tables[i];
        }
    }
    return nullptr;
}

acpi_info acpi_get_info(void) {
    return g_info;
}

void acpi_shutdown(void) {
    kprintf("[ACPI] Initiating ACPI System Poweroff...\n");

    // If FADT PM1a control block is configured, send ACPI sleep/poweroff command
    if (g_info.fadt_pm1a_cnt != 0) {
        outw(static_cast<uint16_t>(g_info.fadt_pm1a_cnt), 0x2000);
    }

    // Standard QEMU/Bochs ACPI poweroff ports
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    // If system didn't power off immediately, halt CPU
    for (;;) {
        asm volatile("cli; hlt");
    }
}

const driver *acpi_get_driver(void) {
    return &g_acpi_drv_instance;
}
