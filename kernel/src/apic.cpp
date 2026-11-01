#include "apic.h"
#include "msr.h"
#include "cpuid.h"
#include "kprint.h"

namespace {
    // Local APIC Register Offsets
    constexpr uint32_t LAPIC_REG_ID          = 0x020;
    constexpr uint32_t LAPIC_REG_VERSION     = 0x030;
    constexpr uint32_t LAPIC_REG_TPR         = 0x080;
    constexpr uint32_t LAPIC_REG_EOI         = 0x0B0;
    constexpr uint32_t LAPIC_REG_SVR         = 0x0F0;
    constexpr uint32_t LAPIC_REG_ESR         = 0x280;
    constexpr uint32_t LAPIC_REG_LVT_LINT0   = 0x350;
    constexpr uint32_t LAPIC_REG_LVT_LINT1   = 0x360;

    // SVR Bits
    constexpr uint32_t SVR_APIC_ENABLE       = 0x100; // Bit 8: Software Enable

    // MSR Bits for APIC Base
    constexpr uint64_t APIC_MSR_GLOBAL_ENABLE = (1ULL << 11);
    constexpr uint64_t APIC_MSR_X2APIC_ENABLE = (1ULL << 10);

    uint64_t g_lapic_phys = 0;
    uint64_t g_lapic_virt = 0;
    uint64_t g_hhdm_offset = 0;
    bool g_is_x2apic = false;
    bool g_lapic_initialized = false;

    // Helper to map a 4KB physical page to a virtual address using Limine's active PML4
    void map_page_4k(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
        uint64_t cr3 = 0;
        asm volatile("mov %%cr3, %0" : "=r"(cr3));
        uint64_t *pml4 = reinterpret_cast<uint64_t *>((cr3 & 0xFFFFFFFFFFFFF000ULL) + g_hhdm_offset);

        size_t pml4_idx = (virt_addr >> 39) & 0x1FF;
        size_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
        size_t pd_idx   = (virt_addr >> 21) & 0x1FF;
        size_t pt_idx   = (virt_addr >> 12) & 0x1FF;

        // PML4 Entry
        if (!(pml4[pml4_idx] & 1)) {
            return;
        }
        uint64_t *pdpt = reinterpret_cast<uint64_t *>((pml4[pml4_idx] & 0xFFFFFFFFFFFFF000ULL) + g_hhdm_offset);

        // PDPT Entry (check 1GB page vs standard)
        if (!(pdpt[pdpt_idx] & 1) || (pdpt[pdpt_idx] & 0x80)) {
            return;
        }
        uint64_t *pd = reinterpret_cast<uint64_t *>((pdpt[pdpt_idx] & 0xFFFFFFFFFFFFF000ULL) + g_hhdm_offset);

        // PD Entry (check 2MB page vs standard)
        if (!(pd[pd_idx] & 1) || (pd[pd_idx] & 0x80)) {
            return;
        }
        uint64_t *pt = reinterpret_cast<uint64_t *>((pd[pd_idx] & 0xFFFFFFFFFFFFF000ULL) + g_hhdm_offset);

        // Map PT entry
        pt[pt_idx] = (phys_addr & 0xFFFFFFFFFFFFF000ULL) | flags;
        asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
    }
}

uint32_t lapic_read(uint32_t reg_offset) {
    if (g_is_x2apic) {
        uint32_t msr = 0x800 + (reg_offset >> 4);
        return static_cast<uint32_t>(rdmsr(msr));
    }
    if (!g_lapic_virt) {
        return 0;
    }
    return *reinterpret_cast<volatile uint32_t *>(g_lapic_virt + reg_offset);
}

void lapic_write(uint32_t reg_offset, uint32_t value) {
    if (g_is_x2apic) {
        uint32_t msr = 0x800 + (reg_offset >> 4);
        wrmsr(msr, value);
        return;
    }
    if (!g_lapic_virt) {
        return;
    }
    *reinterpret_cast<volatile uint32_t *>(g_lapic_virt + reg_offset) = value;
}

void lapic_eoi(void) {
    lapic_write(LAPIC_REG_EOI, 0);
}

void lapic_init(uint64_t hhdm_offset) {
    g_hhdm_offset = hhdm_offset;

    // 1. Check CPUID for APIC and x2APIC support
    cpuid_result feat = cpuid(1);
    bool has_apic = (feat.edx & (1 << 9)) != 0;
    bool has_x2apic = (feat.ecx & (1 << 21)) != 0;

    if (!has_apic) {
        kprintf("[WARN] CPU does not report APIC support.\n");
        return;
    }

    // 2. Read Base Address from IA32_APIC_BASE MSR
    uint64_t apic_msr = rdmsr(IA32_APIC_BASE_MSR);
    g_lapic_phys = apic_msr & 0xFFFFFF000ULL;
    g_lapic_virt = g_lapic_phys + hhdm_offset;

    if (has_x2apic) {
        // Enable x2APIC mode (Global enable + x2APIC enable in MSR)
        wrmsr(IA32_APIC_BASE_MSR, apic_msr | APIC_MSR_GLOBAL_ENABLE | APIC_MSR_X2APIC_ENABLE);
        g_is_x2apic = true;
    } else {
        // Classic xAPIC mode: Ensure global enable is set and map MMIO page
        if (!(apic_msr & APIC_MSR_GLOBAL_ENABLE)) {
            wrmsr(IA32_APIC_BASE_MSR, apic_msr | APIC_MSR_GLOBAL_ENABLE);
        }
        // Map LAPIC MMIO page (Present | Read/Write | Cache Disable)
        map_page_4k(g_lapic_virt, g_lapic_phys, 0x1B);
        g_is_x2apic = false;
    }

    // 3. Clear Task Priority Register to accept all interrupts
    lapic_write(LAPIC_REG_TPR, 0);

    // 4. Clear Error Status Register
    lapic_write(LAPIC_REG_ESR, 0);
    lapic_write(LAPIC_REG_ESR, 0);

    // 5. Mask LINT0 and LINT1 initially
    lapic_write(LAPIC_REG_LVT_LINT0, 0x10000);
    lapic_write(LAPIC_REG_LVT_LINT1, 0x10000);

    // 6. Set Spurious Vector and Software Enable bit (Vector 255 + Bit 8)
    lapic_write(LAPIC_REG_SVR, apic_vectors::SPURIOUS_VECTOR | SVR_APIC_ENABLE);

    g_lapic_initialized = true;
}

bool lapic_is_active(void) {
    if (!g_lapic_initialized) {
        return false;
    }
    uint32_t svr = lapic_read(LAPIC_REG_SVR);
    return (svr & SVR_APIC_ENABLE) != 0;
}

lapic_info lapic_get_info(void) {
    lapic_info info{};
    info.physical_base = g_lapic_phys;
    info.virtual_base  = g_lapic_virt;
    info.is_x2apic     = g_is_x2apic;

    if (g_lapic_initialized) {
        uint32_t id_raw = lapic_read(LAPIC_REG_ID);
        info.id         = g_is_x2apic ? id_raw : ((id_raw >> 24) & 0xFF);
        uint32_t ver    = lapic_read(LAPIC_REG_VERSION);
        info.version    = ver & 0xFF;
        info.max_lvt    = ((ver >> 16) & 0xFF) + 1;
        info.is_enabled = (lapic_read(LAPIC_REG_SVR) & SVR_APIC_ENABLE) != 0;
    }

    return info;
}
