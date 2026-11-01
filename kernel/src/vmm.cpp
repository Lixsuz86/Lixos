#include "vmm.h"
#include "pmm.h"
#include "kprint.h"
#include "panic.h"

extern "C" char __kernel_start[];
extern "C" char __kernel_end[];

namespace {
    uint64_t g_kernel_pml4_phys = 0;
    uint64_t g_hhdm_offset = 0;

    constexpr uint64_t ENTRY_ADDR_MASK = 0x000FFFFFFFFFF000ULL;

    uint64_t *get_next_level(uint64_t *current_table, size_t index, bool allocate, uint64_t flags) {
        uint64_t entry = current_table[index];

        if (entry & vmm_flags::PRESENT) {
            if (flags & vmm_flags::USER) {
                current_table[index] |= vmm_flags::USER;
            }
            uint64_t phys = entry & ENTRY_ADDR_MASK;
            return reinterpret_cast<uint64_t *>(phys + g_hhdm_offset);
        }

        if (!allocate) {
            return nullptr;
        }

        uint64_t new_table_phys = pmm_alloc_page();
        if (!new_table_phys) {
            panic("VMM: Out of physical memory while allocating page table!");
        }

        uint64_t *new_table_virt = reinterpret_cast<uint64_t *>(new_table_phys + g_hhdm_offset);
        for (size_t i = 0; i < 512; ++i) {
            new_table_virt[i] = 0;
        }

        current_table[index] = new_table_phys | vmm_flags::PRESENT | vmm_flags::WRITABLE | (flags & vmm_flags::USER);
        return new_table_virt;
    }
}

void vmm_map_page(uint64_t pml4_phys, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    uint64_t v_aligned = virt_addr & ~0xFFFULL;
    uint64_t p_aligned = phys_addr & ~0xFFFULL;

    uint64_t *pml4 = reinterpret_cast<uint64_t *>(pml4_phys + g_hhdm_offset);

    size_t pml4_idx = (v_aligned >> 39) & 0x1FF;
    size_t pdpt_idx = (v_aligned >> 30) & 0x1FF;
    size_t pd_idx   = (v_aligned >> 21) & 0x1FF;
    size_t pt_idx   = (v_aligned >> 12) & 0x1FF;

    uint64_t *pdpt = get_next_level(pml4, pml4_idx, true, flags);
    uint64_t *pd   = get_next_level(pdpt, pdpt_idx, true, flags);
    uint64_t *pt   = get_next_level(pd, pd_idx, true, flags);

    pt[pt_idx] = p_aligned | flags;
    vmm_invlpg(v_aligned);
}

void vmm_unmap_page(uint64_t pml4_phys, uint64_t virt_addr) {
    uint64_t v_aligned = virt_addr & ~0xFFFULL;

    uint64_t *pml4 = reinterpret_cast<uint64_t *>(pml4_phys + g_hhdm_offset);

    size_t pml4_idx = (v_aligned >> 39) & 0x1FF;
    size_t pdpt_idx = (v_aligned >> 30) & 0x1FF;
    size_t pd_idx   = (v_aligned >> 21) & 0x1FF;
    size_t pt_idx   = (v_aligned >> 12) & 0x1FF;

    uint64_t *pdpt = get_next_level(pml4, pml4_idx, false, 0);
    if (!pdpt) return;
    uint64_t *pd   = get_next_level(pdpt, pdpt_idx, false, 0);
    if (!pd) return;
    uint64_t *pt   = get_next_level(pd, pd_idx, false, 0);
    if (!pt) return;

    pt[pt_idx] = 0;
    vmm_invlpg(v_aligned);
}

void vmm_map_range(uint64_t pml4_phys, uint64_t virt_start, uint64_t phys_start, uint64_t size, uint64_t flags) {
    if (size == 0) return;

    uint64_t virt_base = virt_start & ~0xFFFULL;
    uint64_t phys_base = phys_start & ~0xFFFULL;
    uint64_t offset = virt_start - virt_base;
    uint64_t total_size = size + offset;
    uint64_t pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t i = 0; i < pages; ++i) {
        vmm_map_page(pml4_phys, virt_base + (i * PAGE_SIZE), phys_base + (i * PAGE_SIZE), flags);
    }
}

void vmm_switch_pml4(uint64_t pml4_phys) {
    asm volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

uint64_t vmm_get_kernel_pml4(void) {
    return g_kernel_pml4_phys;
}

uint64_t vmm_get_hhdm_offset(void) {
    return g_hhdm_offset;
}

uint64_t vmm_get_active_pml4(void) {
    uint64_t cr3 = 0;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

uint64_t vmm_create_address_space(void) {
    uint64_t pml4_phys = pmm_alloc_page();
    if (!pml4_phys) {
        return 0;
    }

    uint64_t *pml4_virt = reinterpret_cast<uint64_t *>(pml4_phys + g_hhdm_offset);
    uint64_t *kernel_pml4_virt = reinterpret_cast<uint64_t *>(g_kernel_pml4_phys + g_hhdm_offset);

    // Clean user space (entries 0..255)
    for (size_t i = 0; i < 256; ++i) {
        pml4_virt[i] = 0;
    }

    // Share higher-half kernel space mappings (entries 256..511)
    for (size_t i = 256; i < 512; ++i) {
        pml4_virt[i] = kernel_pml4_virt[i];
    }

    return pml4_phys;
}

void vmm_destroy_address_space(uint64_t pml4_phys) {
    if (!pml4_phys || pml4_phys == g_kernel_pml4_phys) {
        return;
    }

    uint64_t *pml4 = reinterpret_cast<uint64_t *>(pml4_phys + g_hhdm_offset);

    // Only clean lower-half user space entries (0..255)
    for (size_t pml4_idx = 0; pml4_idx < 256; ++pml4_idx) {
        if (!(pml4[pml4_idx] & vmm_flags::PRESENT)) continue;

        uint64_t pdpt_phys = pml4[pml4_idx] & ENTRY_ADDR_MASK;
        uint64_t *pdpt = reinterpret_cast<uint64_t *>(pdpt_phys + g_hhdm_offset);

        for (size_t pdpt_idx = 0; pdpt_idx < 512; ++pdpt_idx) {
            if (!(pdpt[pdpt_idx] & vmm_flags::PRESENT)) continue;

            uint64_t pd_phys = pdpt[pdpt_idx] & ENTRY_ADDR_MASK;
            uint64_t *pd = reinterpret_cast<uint64_t *>(pd_phys + g_hhdm_offset);

            for (size_t pd_idx = 0; pd_idx < 512; ++pd_idx) {
                if (!(pd[pd_idx] & vmm_flags::PRESENT)) continue;

                if (pd[pd_idx] & vmm_flags::HUGE_PAGE) {
                    uint64_t huge_phys = pd[pd_idx] & ENTRY_ADDR_MASK;
                    pmm_free_page(huge_phys);
                } else {
                    uint64_t pt_phys = pd[pd_idx] & ENTRY_ADDR_MASK;
                    uint64_t *pt = reinterpret_cast<uint64_t *>(pt_phys + g_hhdm_offset);

                    for (size_t pt_idx = 0; pt_idx < 512; ++pt_idx) {
                        if (pt[pt_idx] & vmm_flags::PRESENT) {
                            uint64_t page_phys = pt[pt_idx] & ENTRY_ADDR_MASK;
                            pmm_free_page(page_phys);
                        }
                    }
                    pmm_free_page(pt_phys);
                }
            }
            pmm_free_page(pd_phys);
        }
        pmm_free_page(pdpt_phys);
    }

    // Free the process PML4 table itself
    pmm_free_page(pml4_phys);
}

void vmm_init(struct limine_memmap_response *memmap,
              uint64_t kernel_phys_base,
              uint64_t kernel_virt_base,
              uint64_t hhdm_offset) {
    g_hhdm_offset = hhdm_offset;

    // 1. Allocate Master Kernel PML4 page table
    g_kernel_pml4_phys = pmm_alloc_page();
    if (!g_kernel_pml4_phys) {
        panic("VMM: Failed to allocate physical page for master PML4!");
    }

    uint64_t *pml4_virt = reinterpret_cast<uint64_t *>(g_kernel_pml4_phys + g_hhdm_offset);
    for (size_t i = 0; i < 512; ++i) {
        pml4_virt[i] = 0;
    }

    // 2. Map Kernel ELF sections
    uint64_t kernel_size = reinterpret_cast<uint64_t>(__kernel_end) - reinterpret_cast<uint64_t>(__kernel_start);
    vmm_map_range(g_kernel_pml4_phys, kernel_virt_base, kernel_phys_base, kernel_size,
                  vmm_flags::PRESENT | vmm_flags::WRITABLE);

    // 3. Map all memory map regions into HHDM
    for (uint64_t i = 0; i < memmap->entry_count; ++i) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        vmm_map_range(g_kernel_pml4_phys,
                      entry->base + g_hhdm_offset,
                      entry->base,
                      entry->length,
                      vmm_flags::PRESENT | vmm_flags::WRITABLE);
    }

    // 4. Map Local APIC MMIO region into HHDM
    vmm_map_page(g_kernel_pml4_phys,
                 0xFEE00000 + g_hhdm_offset,
                 0xFEE00000,
                 vmm_flags::PRESENT | vmm_flags::WRITABLE | vmm_flags::NO_CACHE);

    // 5. Switch hardware CR3 to new Master Kernel PML4
    vmm_switch_pml4(g_kernel_pml4_phys);
}
