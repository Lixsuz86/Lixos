#include "panic.h"
#include "kprint.h"
#include "fb.h"

namespace {
    const char *get_exception_name(uint64_t vector) {
        switch (vector) {
            case 0:  return "#DE Divide-by-zero Error";
            case 1:  return "#DB Debug";
            case 2:  return "#NMI Non-maskable Interrupt";
            case 3:  return "#BP Breakpoint";
            case 4:  return "#OF Overflow";
            case 5:  return "#BR Bound Range Exceeded";
            case 6:  return "#UD Invalid Opcode";
            case 7:  return "#NM Device Not Available";
            case 8:  return "#DF Double Fault";
            case 9:  return "#MF Coprocessor Segment Overrun";
            case 10: return "#TS Invalid TSS";
            case 11: return "#NP Segment Not Present";
            case 12: return "#SS Stack-Segment Fault";
            case 13: return "#GP General Protection Fault";
            case 14: return "#PF Page Fault";
            case 15: return "#RE Reserved";
            case 16: return "#MF x87 Floating-Point Exception";
            case 17: return "#AC Alignment Check";
            case 18: return "#MC Machine Check";
            case 19: return "#XM SIMD Floating-Point Exception";
            case 20: return "#VE Virtualization Exception";
            case 21: return "#CP Control Protection Exception";
            case 28: return "#HV Hypervisor Injection Exception";
            case 29: return "#VC VMM Communication Exception";
            case 30: return "#SX Security Exception";
            default: return "Unknown CPU Exception";
        }
    }
}

[[noreturn]] void panic(const char *msg, const interrupt_frame *frame) {
    // 1. Disable all interrupts immediately
    asm volatile("cli");

    // 2. Adjust console colors for visual urgency
    if (fb_is_available()) {
        fb_console_set_color(0x00FFFFFF, 0x003A0A0A); // White text on dark crimson red
        fb_console_clear();
    }

    // 3. Print Panic Header
    kprintf("\n");
    kprintf("================================================================================\n");
    kprintf("                               *** KERNEL PANIC ***                             \n");
    kprintf("================================================================================\n\n");

    if (msg) {
        kprintf("[PANIC REASON] %s\n\n", msg);
    }

    // 4. Dump Exception & CPU Registers if interrupt frame is available
    if (frame) {
        kprintf("[EXCEPTION] Vector %u (%s), Error Code: 0x%x\n\n",
                static_cast<unsigned int>(frame->int_no),
                get_exception_name(frame->int_no),
                static_cast<unsigned int>(frame->err_code));

        kprintf("[INSTRUCTION POINTER] RIP: 0x%p  CS: 0x%x  RFLAGS: 0x%p\n",
                reinterpret_cast<void *>(frame->rip),
                static_cast<unsigned int>(frame->cs),
                reinterpret_cast<void *>(frame->rflags));

        kprintf("[STACK REGISTERS]     RSP: 0x%p  SS: 0x%x  RBP: 0x%p\n\n",
                reinterpret_cast<void *>(frame->rsp),
                static_cast<unsigned int>(frame->ss),
                reinterpret_cast<void *>(frame->rbp));

        kprintf("[GENERAL REGISTERS]\n");
        kprintf("  RAX: 0x%p  RBX: 0x%p  RCX: 0x%p  RDX: 0x%p\n",
                reinterpret_cast<void *>(frame->rax),
                reinterpret_cast<void *>(frame->rbx),
                reinterpret_cast<void *>(frame->rcx),
                reinterpret_cast<void *>(frame->rdx));
        kprintf("  RSI: 0x%p  RDI: 0x%p  R8:  0x%p  R9:  0x%p\n",
                reinterpret_cast<void *>(frame->rsi),
                reinterpret_cast<void *>(frame->rdi),
                reinterpret_cast<void *>(frame->r8),
                reinterpret_cast<void *>(frame->r9));
        kprintf("  R10: 0x%p  R11: 0x%p  R12: 0x%p  R13: 0x%p\n",
                reinterpret_cast<void *>(frame->r10),
                reinterpret_cast<void *>(frame->r11),
                reinterpret_cast<void *>(frame->r12),
                reinterpret_cast<void *>(frame->r13));
        kprintf("  R14: 0x%p  R15: 0x%p\n\n",
                reinterpret_cast<void *>(frame->r14),
                reinterpret_cast<void *>(frame->r15));

        // Read Control Registers
        uint64_t cr0 = 0, cr2 = 0, cr3 = 0, cr4 = 0;
        asm volatile("mov %%cr0, %0" : "=r"(cr0));
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        asm volatile("mov %%cr3, %0" : "=r"(cr3));
        asm volatile("mov %%cr4, %0" : "=r"(cr4));

        kprintf("[CONTROL REGISTERS]\n");
        kprintf("  CR0: 0x%p  CR2 (Fault Addr): 0x%p\n",
                reinterpret_cast<void *>(cr0),
                reinterpret_cast<void *>(cr2));
        kprintf("  CR3 (PML4 Base): 0x%p  CR4: 0x%p\n\n",
                reinterpret_cast<void *>(cr3),
                reinterpret_cast<void *>(cr4));
    }

    kprintf("[SYSTEM HALTED] Execution stopped safely. Please reset or reboot.\n");
    kprintf("================================================================================\n");

    // 5. Permanent CPU halt
    for (;;) {
        asm volatile("cli; hlt");
    }
}
