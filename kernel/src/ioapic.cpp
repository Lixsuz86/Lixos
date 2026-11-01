#include "ioapic.h"
#include "vmm.h"
#include "kprint.h"

namespace {
    uint64_t g_ioapic_virt = 0;

    uint32_t ioapic_read(uint8_t reg) {
        if (!g_ioapic_virt) return 0;
        *reinterpret_cast<volatile uint32_t *>(g_ioapic_virt + 0x00) = reg;
        return *reinterpret_cast<volatile uint32_t *>(g_ioapic_virt + 0x10);
    }

    void ioapic_write(uint8_t reg, uint32_t value) {
        if (!g_ioapic_virt) return;
        *reinterpret_cast<volatile uint32_t *>(g_ioapic_virt + 0x00) = reg;
        *reinterpret_cast<volatile uint32_t *>(g_ioapic_virt + 0x10) = value;
    }
}

void ioapic_init(uint64_t hhdm_offset, uint64_t ioapic_phys_base) {
    g_ioapic_virt = ioapic_phys_base + hhdm_offset;

    // Ensure IO-APIC physical page is mapped in master kernel PML4
    uint64_t pml4 = vmm_get_kernel_pml4();
    vmm_map_page(pml4, g_ioapic_virt, ioapic_phys_base,
                 vmm_flags::PRESENT | vmm_flags::WRITABLE | vmm_flags::NO_CACHE);

    uint32_t ver = ioapic_read(0x01);
    uint32_t max_entries = ((ver >> 16) & 0xFF) + 1;

    // Initially mask all redirection entries
    for (uint8_t i = 0; i < max_entries && i < 24; ++i) {
        ioapic_mask_irq(i);
    }
}

void ioapic_set_irq(uint8_t pin, uint8_t vector, uint8_t dest_lapic_id, bool masked) {
    uint32_t low = vector | (masked ? (1 << 16) : 0);
    uint32_t high = static_cast<uint32_t>(dest_lapic_id) << 24;

    ioapic_write(0x10 + (2 * pin), low);
    ioapic_write(0x11 + (2 * pin), high);
}

void ioapic_mask_irq(uint8_t pin) {
    uint32_t low = ioapic_read(0x10 + (2 * pin));
    ioapic_write(0x10 + (2 * pin), low | (1 << 16));
}

void ioapic_unmask_irq(uint8_t pin) {
    uint32_t low = ioapic_read(0x10 + (2 * pin));
    ioapic_write(0x10 + (2 * pin), low & ~(1 << 16));
}
