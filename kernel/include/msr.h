#ifndef LIXOS_MSR_H
#define LIXOS_MSR_H

#include <stdint.h>

// Standard x86 MSR constants
constexpr uint32_t IA32_APIC_BASE_MSR = 0x1B;
constexpr uint64_t IA32_APIC_BASE_MSR_ENABLE = 0x800; // Bit 11

constexpr uint32_t IA32_EFER_MSR   = 0xC0000080;
constexpr uint32_t IA32_STAR_MSR   = 0xC0000081;
constexpr uint32_t IA32_LSTAR_MSR  = 0xC0000082;
constexpr uint32_t IA32_SFMASK_MSR = 0xC0000084;
constexpr uint64_t EFER_SCE        = (1ULL << 0); // Syscall Enable bit

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<uint64_t>(high) << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = static_cast<uint32_t>(val & 0xFFFFFFFF);
    uint32_t high = static_cast<uint32_t>(val >> 32);
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

#endif // LIXOS_MSR_H
