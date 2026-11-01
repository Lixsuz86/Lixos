#ifndef LIXOS_KEYBOARD_H
#define LIXOS_KEYBOARD_H

#include <stdint.h>
#include <stddef.h>
#include "driver.h"

constexpr uint8_t KEYBOARD_IRQ_VECTOR = 0x21;

// Initialize PS/2 Keyboard driver and register IRQ handler
bool keyboard_init(void);

// Read characters from the keyboard buffer (blocking or non-blocking)
size_t keyboard_read(void *buffer, size_t count);

// Check if a key is currently available in the buffer
bool keyboard_has_key(void);

// Get the driver descriptor
const driver *keyboard_get_driver(void);

#endif // LIXOS_KEYBOARD_H
