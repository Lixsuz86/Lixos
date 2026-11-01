#ifndef LIXOS_VMM_H
#define LIXOS_VMM_H

#include <stdint.h>
#include <stddef.h>
#include "limine.h"

namespace vmm_flags {
    constexpr uint64_t PRESENT        = (1ULL << 0);
    constexpr uint64_t WRITABLE       = (1ULL << 1);
    constexpr uint64_t USER           = (1ULL << 2);
    constexpr uint64_t WRITE_THROUGH  = (1ULL << 3);
    constexpr uint64_t NO_CACHE       = (1ULL << 4);
    constexpr uint64_t ACCESSED       = (1ULL << 5);
    constexpr uint64_t DIRTY          = (1ULL << 6);
    constexpr uint64_t HUGE_PAGE      = (1ULL << 7);
    constexpr uint64_t GLOBAL         = (1ULL << 8);
    constexpr uint64_t NO_EXECUTE     = (1ULL << 63);
}

// Initialize Virtual Memory Manager, build kernel PML4, and switch CR3
void vmm_init(struct limine_memmap_response *memmap,
              uint64_t kernel_phys_base,
              uint64_t kernel_virt_base,
              uint64_t hhdm_offset);

// Map a single 4KB virtual page to a physical address in the given PML4
void vmm_map_page(uint64_t pml4_phys, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

// Unmap a single 4KB virtual page from the given PML4
void vmm_unmap_page(uint64_t pml4_phys, uint64_t virt_addr);

// Map a contiguous range of 4KB pages
void vmm_map_range(uint64_t pml4_phys, uint64_t virt_start, uint64_t phys_start, uint64_t size, uint64_t flags);

// Switch active hardware CR3 page table
void vmm_switch_pml4(uint64_t pml4_phys);

// Get the physical address of the master kernel PML4
uint64_t vmm_get_kernel_pml4(void);

// Get the Higher-Half Direct Map (HHDM) virtual offset
uint64_t vmm_get_hhdm_offset(void);

// Read current active hardware CR3
uint64_t vmm_get_active_pml4(void);

// Create an isolated address space for a process (copies kernel space 256..511, zeroes user space 0..255)
uint64_t vmm_create_address_space(void);

// Destroy process address space and free its page tables and user pages
void vmm_destroy_address_space(uint64_t pml4_phys);

// Invalidate a specific virtual page in the TLB
inline void vmm_invlpg(uint64_t virt_addr) {
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

#endif // LIXOS_VMM_H
