#include "scheduler.h"
#include "process.h"
#include "vmm.h"
#include "tss.h"
#include "kprint.h"
#include "irq_lock.h"

namespace {
    volatile bool g_scheduler_enabled = false;
    volatile uint64_t g_schedule_ticks = 0;
}

void scheduler_init(void) {
    g_scheduler_enabled = false;
    g_schedule_ticks = 0;
    kprintf("[ OK ] Preemptive Round-Robin Scheduler Initialized (Time Slice = %u tick)\n",
            DEFAULT_TIME_SLICE);
}

void scheduler_set_enabled(bool enabled) {
    g_scheduler_enabled = enabled;
}

bool scheduler_is_enabled(void) {
    return g_scheduler_enabled;
}

uint64_t scheduler_schedule(interrupt_frame *frame) {
    if (!g_scheduler_enabled || !frame) {
        return reinterpret_cast<uint64_t>(frame);
    }

    g_schedule_ticks = g_schedule_ticks + 1;
    Process *current = process_get_current();

    // 1. Update current process accounting and time slice
    if (current != nullptr) {
        current->saved_rsp = reinterpret_cast<uint64_t>(frame);
        current->total_ticks++;

        if (current->state == ProcessState::RUNNING) {
            if (current->time_slice > 0) {
                current->time_slice--;
                return reinterpret_cast<uint64_t>(frame);
            }
            // Time slice expired: transition to READY for round-robin rotation
            current->state = ProcessState::READY;
            current->time_slice = DEFAULT_TIME_SLICE;
        }
    }

    // 2. Select next READY process in round-robin sequence
    Process *next = process_get_next_ready(current);
    if (!next || next->saved_rsp == 0) {
        if (current != nullptr && current->state == ProcessState::READY) {
            current->state = ProcessState::RUNNING;
        }
        return reinterpret_cast<uint64_t>(frame);
    }

    // 3. Mark next process as RUNNING
    next->state = ProcessState::RUNNING;
    next->time_slice = DEFAULT_TIME_SLICE;

    // 4. Switch CR3 page directory and TSS RSP0 if switching to another process
    if (next != current) {
        process_switch_address_space(next);
    }

    return next->saved_rsp;
}

void scheduler_yield(void) {
    InterruptLock lock;
    Process *cur = process_get_current();
    if (cur != nullptr) {
        cur->time_slice = 0;
    }
    // Trigger APIC Timer interrupt handler vector 0x20
    asm volatile("int $0x20");
}
