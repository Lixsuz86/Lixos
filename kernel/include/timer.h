#ifndef LIXOS_TIMER_H
#define LIXOS_TIMER_H

#include <stdint.h>
#include "idt.h"

// APIC Timer Vector (Vector 32 / 0x20)
constexpr uint8_t APIC_TIMER_VECTOR = 0x20;

// Initialize and start the Local APIC Periodic Timer
void timer_init(uint32_t initial_count = 0x100000);

// Get the number of timer ticks elapsed since boot
uint64_t timer_get_ticks(void);

// Software delay (busy wait) in ticks
void timer_sleep_ticks(uint64_t ticks);

#endif // LIXOS_TIMER_H
