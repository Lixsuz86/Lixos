#ifndef LIXOS_HEAP_H
#define LIXOS_HEAP_H

#include <stdint.h>
#include <stddef.h>

// Initialize the kernel dynamic heap
void heap_init(size_t initial_pages = 64); // 64 pages = 256KB initial heap

// Standard dynamic allocation API
void *kmalloc(size_t size);
void *kcalloc(size_t num, size_t size);
void *kmalloc_aligned(size_t size, size_t alignment);
void  kfree(void *ptr);

// Heap Diagnostic Statistics
struct heap_stats {
    size_t total_heap_bytes;
    size_t allocated_bytes;
    size_t free_bytes;
    size_t active_allocations;
};

heap_stats heap_get_stats(void);

// Comprehensive sanity and leak verification suite
bool heap_run_sanity_check(void);

#endif // LIXOS_HEAP_H
