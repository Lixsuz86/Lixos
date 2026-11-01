#include "pic.h"
#include "io.h"

namespace {
    // PIC I/O Ports
    constexpr uint16_t PIC1_COMMAND = 0x20;
    constexpr uint16_t PIC1_DATA    = 0x21;
    constexpr uint16_t PIC2_COMMAND = 0xA0;
    constexpr uint16_t PIC2_DATA    = 0xA1;

    // Initialization Command Words
    constexpr uint8_t ICW1_INIT     = 0x11; // Initialize + ICW4 needed
    constexpr uint8_t ICW4_8086     = 0x01; // 8086/88 mode
}

void pic_disable(void) {
    // 1. Start initialization sequence in cascade mode
    outb(PIC1_COMMAND, ICW1_INIT);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT);
    io_wait();

    // 2. Remap offsets to 0x20 and 0x28 to avoid collision with CPU exceptions
    outb(PIC1_DATA, 0x20); // Master PIC vector offset
    io_wait();
    outb(PIC2_DATA, 0x28); // Slave PIC vector offset
    io_wait();

    // 3. Tell Master PIC that there is a slave PIC at IRQ2
    outb(PIC1_DATA, 0x04);
    io_wait();
    // Tell Slave PIC its cascade identity (2)
    outb(PIC2_DATA, 0x02);
    io_wait();

    // 4. Set 8086 mode
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    // 5. Mask all IRQs on both Master and Slave PICs
    outb(PIC1_DATA, 0xFF);
    io_wait();
    outb(PIC2_DATA, 0xFF);
    io_wait();
}
