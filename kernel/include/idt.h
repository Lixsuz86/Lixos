#ifndef LIXOS_IDT_H
#define LIXOS_IDT_H

#include <stdint.h>
#include <stddef.h>

// 64-bit IDT Gate Entry
struct [[gnu::packed]] idt_entry {
    uint16_t offset_low;        // Target offset bits 0..15
    uint16_t segment;           // Target code segment selector
    uint8_t  ist;               // Interrupt Stack Table offset (bits 0..2)
    uint8_t  type_attr;         // Type and Attributes (Present, DPL, Gate type)
    uint16_t offset_middle;     // Target offset bits 16..31
    uint32_t offset_high;       // Target offset bits 32..63
    uint32_t reserved;          // Reserved, must be zero
};

// IDT Pointer structure for LIDT instruction
struct [[gnu::packed]] idt_ptr {
    uint16_t limit;
    uint64_t base;
};

// Stack state passed to C++ interrupt/exception handlers
struct [[gnu::packed]] interrupt_frame {
    // Pushed by isr_common stub
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    // Interrupt number and error code
    uint64_t int_no;
    uint64_t err_code;

    // Pushed automatically by CPU hardware
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

// Function signature for ISR handlers
using isr_handler_t = void (*)(interrupt_frame *);

// Initialize the IDT and register default exception handlers
void idt_init(void);

// Set a specific gate in the IDT
void idt_set_gate(uint8_t vector, void *isr_stub, uint8_t flags, uint8_t ist = 0);

// Set DPL for a specific gate (e.g. DPL 3 for user mode syscalls / software interrupts)
void idt_set_dpl(uint8_t vector, uint8_t dpl);

// Register a C++ handler for a specific interrupt vector
void idt_register_handler(uint8_t vector, isr_handler_t handler);

// Central C++ ISR dispatcher called from isr_common
extern "C" uint64_t isr_dispatch(interrupt_frame *frame);

#endif // LIXOS_IDT_H
