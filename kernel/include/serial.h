#ifndef LIXOS_SERIAL_H
#define LIXOS_SERIAL_H

#include <stdint.h>
#include <stddef.h>

// COM1 Base I/O Port
constexpr uint16_t COM1_PORT = 0x3F8;

// Initialize COM1 serial port (115200 baud, 8N1, FIFO enabled)
void serial_init(void);

// Check if serial transmit buffer is empty
bool serial_is_transmit_empty(void);

// Transmit a single character over COM1
void serial_putc(char c);

// Transmit a null-terminated string over COM1
void serial_puts(const char *str);

// Transmit a buffer with specified length over COM1
void serial_write(const char *buf, size_t len);

#endif // LIXOS_SERIAL_H
