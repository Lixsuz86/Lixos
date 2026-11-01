#include "serial.h"
#include "io.h"

// Register offset constants for 16550 UART
namespace {
    constexpr uint16_t REG_DATA         = 0;
    constexpr uint16_t REG_IER          = 1;
    constexpr uint16_t REG_BAUD_LO      = 0;
    constexpr uint16_t REG_BAUD_HI      = 1;
    constexpr uint16_t REG_FCR          = 2;
    constexpr uint16_t REG_LCR          = 3;
    constexpr uint16_t REG_MCR          = 4;
    constexpr uint16_t REG_LSR          = 5;

    // Line Control Register Flags
    constexpr uint8_t LCR_DLAB_ENABLE   = 0x80;
    constexpr uint8_t LCR_8N1           = 0x03;

    // FIFO Control Register Flags
    constexpr uint8_t FCR_INIT          = 0xC7; // Enable FIFO, clear RX/TX, 14-byte threshold

    // Modem Control Register Flags
    constexpr uint8_t MCR_NORMAL_OP     = 0x0B; // RTS, DTR, and OUT2 active

    // Line Status Register Flags
    constexpr uint8_t LSR_TX_EMPTY      = 0x20; // Transmitter holding register empty
}

void serial_init(void) {
    // Disable all UART interrupts
    outb(COM1_PORT + REG_IER, 0x00);

    // Set DLAB to configure baud rate divisor
    outb(COM1_PORT + REG_LCR, LCR_DLAB_ENABLE);

    // Set divisor to 1 -> 115200 baud
    outb(COM1_PORT + REG_BAUD_LO, 0x01);
    outb(COM1_PORT + REG_BAUD_HI, 0x00);

    // Configure 8 bits, no parity, 1 stop bit (8N1)
    outb(COM1_PORT + REG_LCR, LCR_8N1);

    // Enable and clear FIFO buffers
    outb(COM1_PORT + REG_FCR, FCR_INIT);

    // Enable RTS/DTR and auxiliary output
    outb(COM1_PORT + REG_MCR, MCR_NORMAL_OP);
}

bool serial_is_transmit_empty(void) {
    return (inb(COM1_PORT + REG_LSR) & LSR_TX_EMPTY) != 0;
}

void serial_putc(char c) {
    // Transmit carriage return before newline for proper terminal formatting
    if (c == '\n') {
        while (!serial_is_transmit_empty()) {
            asm volatile("pause");
        }
        outb(COM1_PORT + REG_DATA, '\r');
    }

    while (!serial_is_transmit_empty()) {
        asm volatile("pause");
    }
    outb(COM1_PORT + REG_DATA, static_cast<uint8_t>(c));
}

void serial_puts(const char *str) {
    if (!str) {
        return;
    }
    while (*str) {
        serial_putc(*str++);
    }
}

void serial_write(const char *buf, size_t len) {
    if (!buf) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        serial_putc(buf[i]);
    }
}
