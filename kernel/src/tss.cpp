#include "tss.h"
#include "gdt.h"
#include "string.h"
#include "kprint.h"

namespace {
    alignas(16) tss_entry g_kernel_tss{};
}

void tss_init(uint64_t kernel_stack_top) {
    memset(&g_kernel_tss, 0, sizeof(tss_entry));
    g_kernel_tss.rsp0 = kernel_stack_top;
    g_kernel_tss.iomap_base = sizeof(tss_entry); // No I/O permission bitmap

    // Install TSS into GDT and load TR
    gdt_set_tss(reinterpret_cast<uint64_t>(&g_kernel_tss), sizeof(tss_entry) - 1);
}

void tss_set_rsp0(uint64_t kernel_stack_top) {
    g_kernel_tss.rsp0 = kernel_stack_top;
}

tss_entry *tss_get_current(void) {
    return &g_kernel_tss;
}

extern "C" void enter_user_mode(void (*user_func)(), uint64_t user_stack_top) {
    uint16_t user_data_sel = gdt_selectors::USER_DATA;
    uint16_t user_code_sel = gdt_selectors::USER_CODE;

    asm volatile(
        "movw %w0, %%ds\n\t"
        "movw %w0, %%es\n\t"
        "movw %w0, %%fs\n\t"
        "movw %w0, %%gs\n\t"
        "pushq %q0\n\t"           /* SS: USER_DATA (0x18 | 3 = 0x1B) */
        "pushq %1\n\t"            /* RSP: user_stack_top */
        "pushfq\n\t"              /* RFLAGS */
        "popq %%rax\n\t"
        "orq $0x200, %%rax\n\t"   /* Enable IF (interrupts enabled in user mode) */
        "pushq %%rax\n\t"
        "pushq %q2\n\t"           /* CS: USER_CODE (0x20 | 3 = 0x23) */
        "pushq %3\n\t"            /* RIP: user_func */
        "iretq\n\t"
        :
        : "r"(static_cast<uint64_t>(user_data_sel)),
          "r"(user_stack_top),
          "r"(static_cast<uint64_t>(user_code_sel)),
          "r"(reinterpret_cast<uint64_t>(user_func))
        : "rax", "memory"
    );
}
