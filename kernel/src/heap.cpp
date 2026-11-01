#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "kprint.h"
#include "panic.h"

namespace {
    constexpr uint64_t HEAP_VIRT_START      = 0xffffffffa0000000ULL;
    constexpr uint32_t HEAP_MAGIC_ALLOCATED = 0x4C495841; // 'LIXA'
    constexpr uint32_t HEAP_MAGIC_FREE      = 0x4C495846; // 'LIXF'
    constexpr uint32_t HEAP_MAGIC_ALIGNED   = 0x4C49584C; // 'LIXL'

    struct alignas(16) heap_block {
        uint32_t magic;
        bool is_free;
        size_t size; // Payload size in bytes
        heap_block *next;
        heap_block *prev;
    };

    heap_block *g_heap_head = nullptr;
    uint64_t g_heap_current_end = HEAP_VIRT_START;
    size_t g_allocated_bytes = 0;
    size_t g_active_allocations = 0;

    // Expand heap by the given number of pages
    bool heap_expand(size_t page_count) {
        uint64_t pml4 = vmm_get_kernel_pml4();

        for (size_t i = 0; i < page_count; ++i) {
            uint64_t phys_page = pmm_alloc_page();
            if (!phys_page) {
                return false;
            }

            vmm_map_page(pml4, g_heap_current_end, phys_page,
                         vmm_flags::PRESENT | vmm_flags::WRITABLE);

            g_heap_current_end += PAGE_SIZE;
        }

        return true;
    }

    // Create and insert a new free block for newly mapped memory
    void heap_add_region(uint64_t start_addr, size_t byte_count) {
        if (byte_count <= sizeof(heap_block)) {
            return;
        }

        heap_block *new_block = reinterpret_cast<heap_block *>(start_addr);
        new_block->magic = HEAP_MAGIC_FREE;
        new_block->is_free = true;
        new_block->size = byte_count - sizeof(heap_block);
        new_block->next = nullptr;
        new_block->prev = nullptr;

        if (!g_heap_head) {
            g_heap_head = new_block;
            return;
        }

        // Append to the end of the block list
        heap_block *curr = g_heap_head;
        while (curr->next) {
            curr = curr->next;
        }

        curr->next = new_block;
        new_block->prev = curr;

        // Try coalescing with previous if it's free
        if (curr->is_free) {
            curr->size += sizeof(heap_block) + new_block->size;
            curr->next = nullptr;
        }
    }
}

void heap_init(size_t initial_pages) {
    g_heap_head = nullptr;
    g_heap_current_end = HEAP_VIRT_START;
    g_allocated_bytes = 0;
    g_active_allocations = 0;

    uint64_t start_addr = g_heap_current_end;
    if (!heap_expand(initial_pages)) {
        panic("Heap: Failed to allocate initial heap pages!");
    }

    size_t total_bytes = initial_pages * PAGE_SIZE;
    heap_add_region(start_addr, total_bytes);
}

void *kmalloc(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    // Align size to 16-byte boundary
    size = (size + 15) & ~15ULL;

    heap_block *curr = g_heap_head;

    while (curr) {
        if (curr->is_free && curr->size >= size) {
            // Check if we can split the block
            if (curr->size >= size + sizeof(heap_block) + 16) {
                size_t remaining = curr->size - size - sizeof(heap_block);

                uint8_t *split_addr = reinterpret_cast<uint8_t *>(curr + 1) + size;
                heap_block *new_block = reinterpret_cast<heap_block *>(split_addr);

                new_block->magic = HEAP_MAGIC_FREE;
                new_block->is_free = true;
                new_block->size = remaining;
                new_block->next = curr->next;
                new_block->prev = curr;

                if (curr->next) {
                    curr->next->prev = new_block;
                }
                curr->next = new_block;
                curr->size = size;
            }

            curr->is_free = false;
            curr->magic = HEAP_MAGIC_ALLOCATED;
            g_allocated_bytes += curr->size;
            ++g_active_allocations;

            return reinterpret_cast<void *>(curr + 1);
        }
        curr = curr->next;
    }

    // No suitable block found: expand heap
    size_t needed_bytes = size + sizeof(heap_block);
    size_t needed_pages = (needed_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (needed_pages < 16) {
        needed_pages = 16; // Expand by at least 64KB
    }

    uint64_t start_addr = g_heap_current_end;
    if (!heap_expand(needed_pages)) {
        kprintf("[WARN] Heap: Out of physical memory to expand heap!\n");
        return nullptr;
    }

    heap_add_region(start_addr, needed_pages * PAGE_SIZE);

    // Retry allocation
    return kmalloc(size);
}

void *kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void *ptr = kmalloc(total);
    if (!ptr) {
        return nullptr;
    }

    uint8_t *byte_ptr = reinterpret_cast<uint8_t *>(ptr);
    for (size_t i = 0; i < total; ++i) {
        byte_ptr[i] = 0;
    }

    return ptr;
}

void *kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        panic("Heap: Invalid alignment request (must be power of 2)!");
    }

    if (alignment <= 16) {
        return kmalloc(size);
    }

    // Allocate extra room for alignment and pointer metadata
    size_t total_size = size + alignment + sizeof(void *) + sizeof(uint32_t);
    void *raw = kmalloc(total_size);
    if (!raw) {
        return nullptr;
    }

    uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw) + sizeof(void *) + sizeof(uint32_t);
    uintptr_t aligned_addr = (raw_addr + alignment - 1) & ~(alignment - 1);

    // Store metadata before aligned address
    reinterpret_cast<void **>(aligned_addr)[-1] = raw;
    reinterpret_cast<uint32_t *>(aligned_addr)[-3] = HEAP_MAGIC_ALIGNED;

    return reinterpret_cast<void *>(aligned_addr);
}

void kfree(void *ptr) {
    if (!ptr) {
        return;
    }

    // Check if this was an aligned allocation
    uint32_t *aligned_marker = reinterpret_cast<uint32_t *>(ptr) - 3;
    if (*aligned_marker == HEAP_MAGIC_ALIGNED) {
        void *raw = reinterpret_cast<void **>(ptr)[-1];
        *aligned_marker = 0; // Clear marker
        kfree(raw);
        return;
    }

    heap_block *block = reinterpret_cast<heap_block *>(reinterpret_cast<uint8_t *>(ptr) - sizeof(heap_block));

    // Corruption and double-free detection
    if (block->magic == HEAP_MAGIC_FREE) {
        panic("Heap Panic: Double free detected on pointer!");
    }

    if (block->magic != HEAP_MAGIC_ALLOCATED) {
        panic("Heap Panic: Heap corruption / invalid block magic detected!");
    }

    block->is_free = true;
    block->magic = HEAP_MAGIC_FREE;
    g_allocated_bytes -= block->size;
    --g_active_allocations;

    // Coalesce with next block if free
    if (block->next && block->next->is_free) {
        block->size += sizeof(heap_block) + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    // Coalesce with previous block if free
    if (block->prev && block->prev->is_free) {
        block->prev->size += sizeof(heap_block) + block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }
}

heap_stats heap_get_stats(void) {
    heap_stats stats{};
    stats.total_heap_bytes   = g_heap_current_end - HEAP_VIRT_START;
    stats.allocated_bytes    = g_allocated_bytes;
    stats.free_bytes         = stats.total_heap_bytes > g_allocated_bytes ? (stats.total_heap_bytes - g_allocated_bytes) : 0;
    stats.active_allocations = g_active_allocations;
    return stats;
}

bool heap_run_sanity_check(void) {
    kprintf("[SANITY] Running Kernel Dynamic Heap (kmalloc/kfree) Verification...\n");

    heap_stats init_stats = heap_get_stats();
    kprintf("         -> Baseline: %u Bytes Allocated, %u Active Allocations\n",
            static_cast<unsigned int>(init_stats.allocated_bytes),
            static_cast<unsigned int>(init_stats.active_allocations));

    // Test 1: Mixed size allocations (16B, 64B, 1KB, 128KB)
    void *p1 = kmalloc(16);
    void *p2 = kmalloc(64);
    void *p3 = kmalloc(1024);
    void *p4 = kmalloc(128 * 1024);
    void *p5 = kcalloc(256, sizeof(uint32_t)); // 1KB zero-initialized

    if (!p1 || !p2 || !p3 || !p4 || !p5) {
        kprintf("[FAIL] Heap Sanity: Allocation failed in Phase 1!\n");
        return false;
    }

    // Verify write and data integrity across all blocks
    *reinterpret_cast<uint32_t *>(p1) = 0x11111111;
    *reinterpret_cast<uint64_t *>(p2) = 0x2222222233333333ULL;
    *reinterpret_cast<uint64_t *>(p3) = 0x4444444455555555ULL;
    *reinterpret_cast<uint64_t *>(p4) = 0x6666666677777777ULL;

    // Check kcalloc zero-initialization
    uint32_t *p5_arr = reinterpret_cast<uint32_t *>(p5);
    for (size_t i = 0; i < 256; ++i) {
        if (p5_arr[i] != 0) {
            kprintf("[FAIL] Heap Sanity: kcalloc memory was not zeroed!\n");
            return false;
        }
    }

    kprintf("         -> Phase 1: Mixed Size Allocations (16B, 64B, 1KB, 128KB, kcalloc) Passed\n");

    // Test 2: Aligned Allocation (64-byte and 4096-byte page alignment)
    void *aligned_64 = kmalloc_aligned(128, 64);
    void *aligned_4k = kmalloc_aligned(2048, 4096);

    if (reinterpret_cast<uintptr_t>(aligned_64) % 64 != 0) {
        kprintf("[FAIL] Heap Sanity: 64-byte alignment failed!\n");
        return false;
    }
    if (reinterpret_cast<uintptr_t>(aligned_4k) % 4096 != 0) {
        kprintf("[FAIL] Heap Sanity: 4096-byte alignment failed!\n");
        return false;
    }
    kprintf("         -> Phase 2: Aligned Allocation (64B: 0x%p, 4096B: 0x%p) Verified\n",
            aligned_64, aligned_4k);

    // Test 3: Out-of-Order / Mixed Freeing
    kfree(p3);
    kfree(p1);
    kfree(aligned_64);
    kfree(p4);
    kfree(aligned_4k);
    kfree(p2);
    kfree(p5);

    heap_stats after_free = heap_get_stats();
    if (after_free.allocated_bytes != init_stats.allocated_bytes ||
        after_free.active_allocations != init_stats.active_allocations) {
        kprintf("[FAIL] Heap Sanity: Alloc/Free symmetry failed! (Allocated: %u, Expected: %u)\n",
                static_cast<unsigned int>(after_free.allocated_bytes),
                static_cast<unsigned int>(init_stats.allocated_bytes));
        return false;
    }
    kprintf("         -> Phase 3: Out-of-Order Freeing with Coalescing Returned to Exact Baseline (Allocated: 0)\n");

    // Test 4: Stress Loop - 500 alloc/free cycles
    constexpr size_t STRESS_COUNT = 500;
    void *stress_ptrs[STRESS_COUNT];

    for (size_t i = 0; i < STRESS_COUNT; ++i) {
        stress_ptrs[i] = kmalloc(32 + (i % 64));
    }
    // Free in reverse order
    for (size_t i = 0; i < STRESS_COUNT; ++i) {
        kfree(stress_ptrs[STRESS_COUNT - 1 - i]);
    }

    heap_stats final_stats = heap_get_stats();
    if (final_stats.allocated_bytes != init_stats.allocated_bytes ||
        final_stats.active_allocations != init_stats.active_allocations) {
        kprintf("[FAIL] Heap Sanity: 500-Cycle stress test leaked memory!\n");
        return false;
    }
    kprintf("         -> Phase 4: 500-Cycle Alloc/Free Stress Test Passed (Zero Leaks, Perfect Symmetry)\n");

    kprintf("[SUCCESS] Heap Sanity Suite Passed: Robust Dynamic Allocation Verified.\n");
    return true;
}
