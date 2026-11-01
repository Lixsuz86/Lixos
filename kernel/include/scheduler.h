#ifndef LIXOS_SCHEDULER_H
#define LIXOS_SCHEDULER_H

#include <stdint.h>
#include <stddef.h>
#include "idt.h"

// Default time slice allocated to a process in timer ticks
constexpr uint32_t DEFAULT_TIME_SLICE = 1;

// Initialize the scheduler subsystem
void scheduler_init(void);

// Enable or disable scheduler preemption
void scheduler_set_enabled(bool enabled);
bool scheduler_is_enabled(void);

// Schedule next process on timer interrupt (called from isr_dispatch)
// Returns new stack pointer (%rsp) to load before iretq
uint64_t scheduler_schedule(interrupt_frame *frame);

// Voluntarily yield CPU time to the next ready process
void scheduler_yield(void);

#endif // LIXOS_SCHEDULER_H
