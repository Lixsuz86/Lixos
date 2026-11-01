#include "timer.h"
#include "apic.h"
#include "idt.h"

namespace {
    constexpr uint32_t LAPIC_REG_LVT_TIMER     = 0x320;
    constexpr uint32_t LAPIC_REG_TIMER_INITCNT = 0x380;
    constexpr uint32_t LAPIC_REG_TIMER_DIV     = 0x3E0;

    // LVT Timer Modes
    constexpr uint32_t LVT_TIMER_PERIODIC      = (1 << 17); // Periodic mode
    constexpr uint32_t LAPIC_TIMER_DIV_16      = 0x03;      // Divide by 16

    volatile uint64_t g_ticks = 0;

    void timer_isr(interrupt_frame *frame) {
        g_ticks = g_ticks + 1;
        lapic_eoi();
        (void)frame;
    }
}

void timer_init(uint32_t initial_count) {
    g_ticks = 0;

    // 1. Register Timer ISR on vector 0x20 (32)
    idt_register_handler(APIC_TIMER_VECTOR, timer_isr);

    // 2. Set Divide Configuration Register to Divide by 16
    lapic_write(LAPIC_REG_TIMER_DIV, LAPIC_TIMER_DIV_16);

    // 3. Configure LVT Timer to Periodic Mode with Vector 0x20
    lapic_write(LAPIC_REG_LVT_TIMER, LVT_TIMER_PERIODIC | APIC_TIMER_VECTOR);

    // 4. Set Initial Count Register to start periodic ticking
    lapic_write(LAPIC_REG_TIMER_INITCNT, initial_count);
}

uint64_t timer_get_ticks(void) {
    return g_ticks;
}

void timer_sleep_ticks(uint64_t ticks) {
    uint64_t target = g_ticks + ticks;
    while (g_ticks < target) {
        asm volatile("pause");
    }
}
