#include "idt.h"
#include "panic.h"
#include "gdt.h"
#include "apic.h"
#include "scheduler.h"

extern "C" void *isr_stub_table[256];

namespace {
    constexpr size_t IDT_ENTRY_COUNT = 256;

    idt_entry g_idt_entries[IDT_ENTRY_COUNT];
    idt_ptr g_idt_descriptor;
    isr_handler_t g_isr_handlers[IDT_ENTRY_COUNT];

    void default_exception_handler(interrupt_frame *frame) {
        panic("Unhandled CPU Exception", frame);
    }

    void default_irq_handler(interrupt_frame *frame) {
        // Send EOI to APIC for unhandled hardware IRQs to prevent bus lockup
        lapic_eoi();
        (void)frame;
    }
}

// C++ dispatcher called from isr_common in isr_stubs.S
extern "C" uint64_t isr_dispatch(interrupt_frame *frame) {
    if (!frame) {
        panic("Null interrupt frame in ISR dispatch");
    }

    uint8_t vec = static_cast<uint8_t>(frame->int_no);
    if (g_isr_handlers[vec] != nullptr) {
        g_isr_handlers[vec](frame);
    } else if (vec < 32) {
        default_exception_handler(frame);
    } else {
        default_irq_handler(frame);
    }

    // If timer interrupt fired and scheduler is active, perform context switch
    if (vec == 0x20 && scheduler_is_enabled()) {
        return scheduler_schedule(frame);
    }

    return reinterpret_cast<uint64_t>(frame);
}

void idt_set_gate(uint8_t vector, void *isr_stub, uint8_t flags, uint8_t ist) {
    uint64_t addr = reinterpret_cast<uint64_t>(isr_stub);

    g_idt_entries[vector].offset_low    = static_cast<uint16_t>(addr & 0xFFFF);
    g_idt_entries[vector].segment       = gdt_selectors::KERNEL_CODE;
    g_idt_entries[vector].ist           = static_cast<uint8_t>(ist & 0x07);
    g_idt_entries[vector].type_attr     = flags;
    g_idt_entries[vector].offset_middle = static_cast<uint16_t>((addr >> 16) & 0xFFFF);
    g_idt_entries[vector].offset_high   = static_cast<uint32_t>((addr >> 32) & 0xFFFFFFFF);
    g_idt_entries[vector].reserved      = 0;
}

void idt_set_dpl(uint8_t vector, uint8_t dpl) {
    g_idt_entries[vector].type_attr = (g_idt_entries[vector].type_attr & ~0x60) | (static_cast<uint8_t>(dpl & 3) << 5);
}

void idt_register_handler(uint8_t vector, isr_handler_t handler) {
    g_isr_handlers[vector] = handler;
}

void idt_init(void) {
    // 1. Clear handler table
    for (size_t i = 0; i < IDT_ENTRY_COUNT; ++i) {
        g_isr_handlers[i] = nullptr;
    }

    // 2. Install all 256 gates
    // 0x8E = Present (0x80) | DPL 0 (0x00) | 64-bit Interrupt Gate (0x0E)
    constexpr uint8_t INTERRUPT_GATE_FLAGS = 0x8E;
    for (size_t i = 0; i < IDT_ENTRY_COUNT; ++i) {
        idt_set_gate(static_cast<uint8_t>(i), isr_stub_table[i], INTERRUPT_GATE_FLAGS);
    }

    // 3. Configure descriptor and load with LIDT
    g_idt_descriptor.limit = sizeof(g_idt_entries) - 1;
    g_idt_descriptor.base  = reinterpret_cast<uint64_t>(&g_idt_entries[0]);

    asm volatile("lidt (%0)" : : "r"(&g_idt_descriptor) : "memory");
}
