#include "pmm.h"
#include "kprint.h"
#include "panic.h"

namespace {
    uint64_t *g_bitmap = nullptr;
    uint64_t g_total_pages = 0;
    uint64_t g_usable_pages = 0;
    uint64_t g_free_pages = 0;
    uint64_t g_used_pages = 0;
    uint64_t g_total_memory = 0;
    uint64_t g_usable_memory = 0;
    uint64_t g_hhdm_offset = 0;
    uint64_t g_last_scanned_idx = 0;

    inline void bitmap_set(uint64_t page_idx) {
        g_bitmap[page_idx / 64] |= (1ULL << (page_idx % 64));
    }

    inline void bitmap_clear(uint64_t page_idx) {
        g_bitmap[page_idx / 64] &= ~(1ULL << (page_idx % 64));
    }

    inline bool bitmap_test(uint64_t page_idx) {
        return (g_bitmap[page_idx / 64] & (1ULL << (page_idx % 64))) != 0;
    }
}

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    if (!memmap || memmap->entry_count == 0) {
        panic("PMM: Invalid memory map response from bootloader!");
    }

    g_hhdm_offset = hhdm_offset;
    g_total_memory = 0;
    g_usable_memory = 0;

    // 1. Calculate highest usable/reclaimable RAM physical address and total usable RAM
    uint64_t highest_ram_addr = 0;

    for (uint64_t i = 0; i < memmap->entry_count; ++i) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t top = entry->base + entry->length;

        if (entry->type == LIMINE_MEMMAP_USABLE ||
            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            entry->type == LIMINE_MEMMAP_KERNEL_AND_MODULES) {
            if (top > highest_ram_addr) {
                highest_ram_addr = top;
            }
        }

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            g_usable_memory += entry->length;
        }
    }

    g_total_memory = highest_ram_addr;
    g_total_pages = highest_ram_addr / PAGE_SIZE;
    g_usable_pages = g_usable_memory / PAGE_SIZE;

    // 2. Calculate bitmap size in bytes and pages
    uint64_t bitmap_size_bytes = ((g_total_pages + 63) / 64) * sizeof(uint64_t);
    uint64_t bitmap_pages = (bitmap_size_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    // 3. Find a usable memory region large enough to hold the bitmap
    uint64_t bitmap_phys_addr = 0;
    for (uint64_t i = 0; i < memmap->entry_count; ++i) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size_bytes) {
            bitmap_phys_addr = entry->base;
            break;
        }
    }

    if (bitmap_phys_addr == 0) {
        panic("PMM: Could not find usable physical memory region for allocation bitmap!");
    }

    g_bitmap = reinterpret_cast<uint64_t *>(bitmap_phys_addr + g_hhdm_offset);

    // 4. Initially mark ALL pages as used / reserved (all 1s)
    uint64_t bitmap_words = (g_total_pages + 63) / 64;
    for (uint64_t i = 0; i < bitmap_words; ++i) {
        g_bitmap[i] = ~0ULL;
    }

    g_free_pages = 0;
    g_used_pages = g_total_pages;

    // 5. Mark usable memory regions as free (0s)
    for (uint64_t i = 0; i < memmap->entry_count; ++i) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        uint64_t start_page = (entry->base + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t end_page = (entry->base + entry->length) / PAGE_SIZE;

        for (uint64_t p = start_page; p < end_page && p < g_total_pages; ++p) {
            bitmap_clear(p);
            ++g_free_pages;
            --g_used_pages;
        }
    }

    // 6. Protect first 1MB (BIOS/IVT/BDA/EBDA/ROM)
    constexpr uint64_t FIRST_1MB_PAGES = (1024 * 1024) / PAGE_SIZE;
    for (uint64_t p = 0; p < FIRST_1MB_PAGES && p < g_total_pages; ++p) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            --g_free_pages;
            ++g_used_pages;
        }
    }

    // 7. Protect the PMM bitmap's own pages
    uint64_t bitmap_start_page = bitmap_phys_addr / PAGE_SIZE;
    for (uint64_t p = 0; p < bitmap_pages; ++p) {
        uint64_t page_idx = bitmap_start_page + p;
        if (page_idx < g_total_pages && !bitmap_test(page_idx)) {
            bitmap_set(page_idx);
            --g_free_pages;
            ++g_used_pages;
        }
    }

    g_last_scanned_idx = FIRST_1MB_PAGES;
}

uint64_t pmm_alloc_page(void) {
    if (g_free_pages == 0) {
        return 0; // Out of memory
    }

    uint64_t words = (g_total_pages + 63) / 64;
    uint64_t start_word = g_last_scanned_idx / 64;

    for (uint64_t w = 0; w < words; ++w) {
        uint64_t word_idx = (start_word + w) % words;
        uint64_t val = g_bitmap[word_idx];

        if (val != ~0ULL) {
            // Find first free bit (0)
            int bit = __builtin_ctzll(~val);
            uint64_t page_idx = word_idx * 64 + static_cast<uint64_t>(bit);

            if (page_idx < g_total_pages) {
                bitmap_set(page_idx);
                --g_free_pages;
                ++g_used_pages;
                g_last_scanned_idx = page_idx + 1;
                return page_idx * PAGE_SIZE;
            }
        }
    }

    return 0; // No free page found
}

uint64_t pmm_alloc_pages(size_t count) {
    if (count == 0) {
        return 0;
    }
    if (count == 1) {
        return pmm_alloc_page();
    }
    if (count > g_free_pages) {
        return 0;
    }

    uint64_t consecutive = 0;
    uint64_t start_page = 0;

    for (uint64_t p = 0; p < g_total_pages; ++p) {
        if (!bitmap_test(p)) {
            if (consecutive == 0) {
                start_page = p;
            }
            ++consecutive;
            if (consecutive == count) {
                // Found contiguous block: mark as allocated
                for (uint64_t i = 0; i < count; ++i) {
                    bitmap_set(start_page + i);
                }
                g_free_pages -= count;
                g_used_pages += count;
                return start_page * PAGE_SIZE;
            }
        } else {
            consecutive = 0;
        }
    }

    return 0; // Not enough contiguous memory
}

void pmm_free_page(uint64_t phys_addr) {
    if (phys_addr % PAGE_SIZE != 0) {
        panic("PMM: Attempted to free unaligned physical address!");
    }

    uint64_t page_idx = phys_addr / PAGE_SIZE;

    if (page_idx >= g_total_pages) {
        panic("PMM: Attempted to free out-of-bounds physical page!");
    }

    if (!bitmap_test(page_idx)) {
        kprintf("[WARN] PMM: Double free detected on physical page 0x%p\n", reinterpret_cast<void *>(phys_addr));
        return;
    }

    bitmap_clear(page_idx);
    ++g_free_pages;
    --g_used_pages;

    if (page_idx < g_last_scanned_idx) {
        g_last_scanned_idx = page_idx;
    }
}

void pmm_free_pages(uint64_t phys_addr, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        pmm_free_page(phys_addr + (i * PAGE_SIZE));
    }
}

pmm_stats pmm_get_stats(void) {
    pmm_stats stats{};
    stats.total_memory  = g_total_memory;
    stats.usable_memory = g_usable_memory;
    stats.total_pages   = g_total_pages;
    stats.used_pages    = g_used_pages;
    stats.free_pages    = g_free_pages;
    return stats;
}

bool pmm_run_sanity_check(void) {
    kprintf("[SANITY] Running Physical Memory Manager Alloc/Free Symmetry Check...\n");

    pmm_stats initial_stats = pmm_get_stats();
    uint64_t initial_free = initial_stats.free_pages;

    kprintf("         -> Initial Free Pages: %u (%u MB)\n",
            static_cast<unsigned int>(initial_free),
            static_cast<unsigned int>((initial_free * PAGE_SIZE) / (1024 * 1024)));

    // Test 1: Allocate 1000 individual pages
    constexpr size_t TEST_PAGE_COUNT = 1000;
    uint64_t pages[TEST_PAGE_COUNT];

    for (size_t i = 0; i < TEST_PAGE_COUNT; ++i) {
        pages[i] = pmm_alloc_page();
        if (pages[i] == 0) {
            kprintf("[FAIL] PMM Sanity: Failed to allocate page %u!\n", static_cast<unsigned int>(i));
            return false;
        }
        if (pages[i] % PAGE_SIZE != 0) {
            kprintf("[FAIL] PMM Sanity: Page %u is not 4KB aligned (0x%p)!\n",
                    static_cast<unsigned int>(i), reinterpret_cast<void *>(pages[i]));
            return false;
        }

        // Write test pattern into physical page through HHDM
        uint64_t *virt_ptr = reinterpret_cast<uint64_t *>(pages[i] + g_hhdm_offset);
        *virt_ptr = 0xDEADBEEFCAFEBABEULL ^ pages[i];
    }

    pmm_stats mid_stats = pmm_get_stats();
    if (mid_stats.free_pages != initial_free - TEST_PAGE_COUNT) {
        kprintf("[FAIL] PMM Sanity: Free count mismatch after allocation! Expected %u, got %u\n",
                static_cast<unsigned int>(initial_free - TEST_PAGE_COUNT),
                static_cast<unsigned int>(mid_stats.free_pages));
        return false;
    }
    kprintf("         -> Phase 1: 1000 Pages Allocated & Verified (Free: %u)\n",
            static_cast<unsigned int>(mid_stats.free_pages));

    // Test 2: Free all 1000 pages
    for (size_t i = 0; i < TEST_PAGE_COUNT; ++i) {
        pmm_free_page(pages[i]);
    }

    pmm_stats after_free_stats = pmm_get_stats();
    if (after_free_stats.free_pages != initial_free) {
        kprintf("[FAIL] PMM Sanity: Leak or corruption detected! Expected %u, got %u\n",
                static_cast<unsigned int>(initial_free),
                static_cast<unsigned int>(after_free_stats.free_pages));
        return false;
    }
    kprintf("         -> Phase 2: 1000 Pages Freed with Exact Symmetry (Free: %u == %u)\n",
            static_cast<unsigned int>(after_free_stats.free_pages),
            static_cast<unsigned int>(initial_free));

    // Test 3: Contiguous Allocation (256 pages = 1MB)
    constexpr size_t CONTIGUOUS_PAGES = 256;
    uint64_t block = pmm_alloc_pages(CONTIGUOUS_PAGES);
    if (block == 0) {
        kprintf("[FAIL] PMM Sanity: Failed to allocate contiguous block of %u pages!\n",
                static_cast<unsigned int>(CONTIGUOUS_PAGES));
        return false;
    }

    pmm_stats contig_alloc_stats = pmm_get_stats();
    if (contig_alloc_stats.free_pages != initial_free - CONTIGUOUS_PAGES) {
        kprintf("[FAIL] PMM Sanity: Free count mismatch after contiguous alloc!\n");
        return false;
    }

    // Free contiguous block
    pmm_free_pages(block, CONTIGUOUS_PAGES);

    pmm_stats final_stats = pmm_get_stats();
    if (final_stats.free_pages != initial_free) {
        kprintf("[FAIL] PMM Sanity: Contiguous free symmetry failed! Expected %u, got %u\n",
                static_cast<unsigned int>(initial_free),
                static_cast<unsigned int>(final_stats.free_pages));
        return false;
    }

    kprintf("         -> Phase 3: Contiguous 256-Page (1MB) Block Alloc/Free Exact Symmetry (Free: %u)\n",
            static_cast<unsigned int>(final_stats.free_pages));

    kprintf("[SUCCESS] PMM Sanity Check Passed: Zero Leaks, Perfect Symmetry.\n");
    return true;
}
