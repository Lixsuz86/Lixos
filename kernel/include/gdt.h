#ifndef LIXOS_GDT_H
#define LIXOS_GDT_H

#include <stdint.h>

// Segment Selectors
namespace gdt_selectors {
    constexpr uint16_t KERNEL_CODE = 0x08;
    constexpr uint16_t KERNEL_DATA = 0x10;
    constexpr uint16_t USER_DATA   = 0x18 | 3; // RPL = 3 (Index 3)
    constexpr uint16_t USER_CODE   = 0x20 | 3; // RPL = 3 (Index 4)
    constexpr uint16_t TSS         = 0x28;     // Index 5 & 6 (16-byte TSS descriptor)
}

// 8-byte GDT Entry structure
struct [[gnu::packed]] gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
};

// GDT Pointer structure for LGDT instruction
struct [[gnu::packed]] gdt_ptr {
    uint16_t limit;
    uint64_t base;
};

// Segment register inspection struct
struct segment_registers {
    uint16_t cs;
    uint16_t ds;
    uint16_t ss;
    uint16_t es;
    uint16_t fs;
    uint16_t gs;
};

// Initialize and load the Global Descriptor Table (GDT)
void gdt_init(void);

// Install a 64-bit TSS descriptor into GDT at index 5/6 and load TR
void gdt_set_tss(uint64_t tss_base, uint32_t tss_limit);

// Read current CPU segment register values
segment_registers gdt_get_registers(void);

#endif // LIXOS_GDT_H
