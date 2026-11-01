#ifndef LIXOS_TSS_H
#define LIXOS_TSS_H

#include <stdint.h>
#include <stddef.h>

// 64-bit Task State Segment (TSS) structure
struct [[gnu::packed]] tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;       // Ring 0 stack pointer (loaded upon privilege level change)
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];     // Interrupt Stack Table pointers (IST1 .. IST7)
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base; // I/O Map Base Address
};

// Initialize TSS and install TSS descriptor into GDT
void tss_init(uint64_t kernel_stack_top);

// Update active Ring 0 stack pointer in TSS
void tss_set_rsp0(uint64_t kernel_stack_top);

// Get pointer to the active kernel TSS
tss_entry *tss_get_current(void);

// Transition to Ring 3 (User Mode) using iretq
extern "C" void enter_user_mode(void (*user_func)(), uint64_t user_stack_top);

#endif // LIXOS_TSS_H
