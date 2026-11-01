#ifndef LIXOS_APIC_H
#define LIXOS_APIC_H

#include <stdint.h>
#include <stddef.h>

// Standard APIC Interrupt Vectors
namespace apic_vectors {
    constexpr uint8_t TIMER_VECTOR    = 0x20; // Vector 32 (first user IRQ vector)
    constexpr uint8_t SPURIOUS_VECTOR = 0xFF; // Vector 255 for spurious interrupts
}

// Local APIC Information structure
struct lapic_info {
    uint64_t physical_base;
    uint64_t virtual_base;
    uint32_t id;
    uint32_t version;
    uint32_t max_lvt;
    bool     is_x2apic;
    bool     is_enabled;
};

// Initialize Local APIC (detects x2APIC / xAPIC)
void lapic_init(uint64_t hhdm_offset);

// Check if Local APIC is active
bool lapic_is_active(void);

// Get current Local APIC hardware diagnostic information
lapic_info lapic_get_info(void);

// Read from a 32-bit Local APIC register (MSR in x2APIC or MMIO in xAPIC)
uint32_t lapic_read(uint32_t reg_offset);

// Write to a 32-bit Local APIC register
void lapic_write(uint32_t reg_offset, uint32_t value);

// Send End Of Interrupt (EOI) to Local APIC
void lapic_eoi(void);

#endif // LIXOS_APIC_H
