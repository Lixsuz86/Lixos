#include "gdt.h"
#include "kprint.h"

namespace {
    constexpr size_t GDT_ENTRY_COUNT = 7;

    // Static GDT table and pointer
    gdt_entry g_gdt_entries[GDT_ENTRY_COUNT];
    gdt_ptr g_gdt_descriptor;

    void set_gdt_entry(size_t index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
        g_gdt_entries[index].base_low    = static_cast<uint16_t>(base & 0xFFFF);
        g_gdt_entries[index].base_middle = static_cast<uint8_t>((base >> 16) & 0xFF);
        g_gdt_entries[index].base_high   = static_cast<uint8_t>((base >> 24) & 0xFF);

        g_gdt_entries[index].limit_low   = static_cast<uint16_t>(limit & 0xFFFF);
        g_gdt_entries[index].granularity = static_cast<uint8_t>((limit >> 16) & 0x0F) | static_cast<uint8_t>(flags & 0xF0);

        g_gdt_entries[index].access      = access;
    }

    void gdt_flush(const gdt_ptr *ptr) {
        asm volatile(
            "lgdt (%0)\n\t"
            "movw $0x10, %%ax\n\t"
            "movw %%ax, %%ds\n\t"
            "movw %%ax, %%es\n\t"
            "movw %%ax, %%ss\n\t"
            "movw $0x00, %%ax\n\t"
            "movw %%ax, %%fs\n\t"
            "movw %%ax, %%gs\n\t"
            "pushq $0x08\n\t"
            "lea 1f(%%rip), %%rax\n\t"
            "pushq %%rax\n\t"
            "lretq\n\t"
            "1:\n\t"
            :
            : "r"(ptr)
            : "rax", "memory"
        );
    }
}

void gdt_init(void) {
    // 0x00: Null Descriptor
    set_gdt_entry(0, 0, 0, 0x00, 0x00);

    // 0x08: Kernel 64-bit Code Segment (Present, Ring 0, Executable, Readable, Long mode 64-bit)
    set_gdt_entry(1, 0, 0, 0x9A, 0x20);

    // 0x10: Kernel Data Segment (Present, Ring 0, Data, Writable)
    set_gdt_entry(2, 0, 0, 0x92, 0x00);

    // 0x18: User Data Segment (Present, Ring 3, Data, Writable)
    set_gdt_entry(3, 0, 0, 0xF2, 0x00);

    // 0x20: User 64-bit Code Segment (Present, Ring 3, Executable, Readable, Long mode 64-bit)
    set_gdt_entry(4, 0, 0, 0xFA, 0x20);

    // 0x28: TSS Placeholders (Index 5 & 6)
    set_gdt_entry(5, 0, 0, 0x00, 0x00);
    set_gdt_entry(6, 0, 0, 0x00, 0x00);

    // Setup descriptor
    g_gdt_descriptor.limit = sizeof(g_gdt_entries) - 1;
    g_gdt_descriptor.base  = reinterpret_cast<uint64_t>(&g_gdt_entries[0]);

    // Load GDT and reload segment registers
    gdt_flush(&g_gdt_descriptor);
}

void gdt_set_tss(uint64_t tss_base, uint32_t tss_limit) {
    // Low 8 bytes at index 5: Present (0x80), Ring 0 (0x00), Type 9 (64-bit TSS available) = 0x89
    set_gdt_entry(5, static_cast<uint32_t>(tss_base & 0xFFFFFFFF), tss_limit, 0x89, 0x00);

    // High 8 bytes at index 6: Upper 32 bits of 64-bit TSS base address
    uint32_t *high_entry = reinterpret_cast<uint32_t *>(&g_gdt_entries[6]);
    high_entry[0] = static_cast<uint32_t>((tss_base >> 32) & 0xFFFFFFFF);
    high_entry[1] = 0; // Reserved

    // Reload GDT to make TSS visible
    g_gdt_descriptor.limit = sizeof(g_gdt_entries) - 1;
    asm volatile("lgdt (%0)" : : "r"(&g_gdt_descriptor) : "memory");

    // Load Task Register (LTR) with selector 0x28
    asm volatile("ltr %%ax" : : "a"(gdt_selectors::TSS) : "memory");
}

segment_registers gdt_get_registers(void) {
    segment_registers regs{};
    asm volatile("mov %%cs, %0" : "=r"(regs.cs));
    asm volatile("mov %%ds, %0" : "=r"(regs.ds));
    asm volatile("mov %%ss, %0" : "=r"(regs.ss));
    asm volatile("mov %%es, %0" : "=r"(regs.es));
    asm volatile("mov %%fs, %0" : "=r"(regs.fs));
    asm volatile("mov %%gs, %0" : "=r"(regs.gs));
    return regs;
}
