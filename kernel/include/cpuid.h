#include "msr.h"
#include <stdint.h>

struct cpuid_result {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

static inline cpuid_result cpuid(uint32_t leaf, uint32_t subleaf = 0) {
    cpuid_result res{};
    asm volatile(
        "cpuid"
        : "=a"(res.eax), "=b"(res.ebx), "=c"(res.ecx), "=d"(res.edx)
        : "a"(leaf), "c"(subleaf)
    );
    return res;
}
