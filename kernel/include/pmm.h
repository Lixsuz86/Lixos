#ifndef LIXOS_PMM_H
#define LIXOS_PMM_H

#include <stdint.h>
#include <stddef.h>
#include "limine.h"

constexpr uint64_t PAGE_SIZE = 4096;

// Memory statistics structure
struct pmm_stats {
    uint64_t total_memory;      // Total addressable physical memory in bytes
    uint64_t usable_memory;     // Usable RAM reported by bootloader
    uint64_t total_pages;       // Total number of 4KB pages in address space
    uint64_t used_pages;        // Currently allocated / reserved pages
    uint64_t free_pages;        // Currently free and available pages
};

// Initialize Physical Memory Manager from Limine memory map
void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

// Allocate a single 4KB physical page (returns physical address, 0 on failure)
uint64_t pmm_alloc_page(void);

// Allocate multiple contiguous 4KB physical pages (returns base physical address)
uint64_t pmm_alloc_pages(size_t count);

// Free a previously allocated 4KB physical page
void pmm_free_page(uint64_t phys_addr);

// Free multiple contiguous 4KB physical pages
void pmm_free_pages(uint64_t phys_addr, size_t count);

// Retrieve current physical memory statistics
pmm_stats pmm_get_stats(void);

// Run exhaustive allocation/free symmetry and leak verification test
bool pmm_run_sanity_check(void);

#endif // LIXOS_PMM_H
