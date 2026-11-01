#ifndef LIXOS_IRQ_LOCK_H
#define LIXOS_IRQ_LOCK_H

#include <stdint.h>

// Save RFLAGS and disable interrupts (CLI)
static inline uint64_t irq_save(void) {
    uint64_t flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

// Restore previous interrupt state from saved RFLAGS
static inline void irq_restore(uint64_t flags) {
    if (flags & 0x200) { // Bit 9 is Interrupt Flag (IF)
        asm volatile("sti" : : : "memory");
    } else {
        asm volatile("cli" : : : "memory");
    }
}

// RAII Critical Section Guard to guarantee thread-safe CLI/STI pairing
class InterruptLock {
    uint64_t flags_;
public:
    InterruptLock() : flags_(irq_save()) {}
    ~InterruptLock() { irq_restore(flags_); }

    InterruptLock(const InterruptLock &) = delete;
    InterruptLock &operator=(const InterruptLock &) = delete;
};

#endif // LIXOS_IRQ_LOCK_H
