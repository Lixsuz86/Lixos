#include "keyboard.h"
#include "io.h"
#include "idt.h"
#include "apic.h"
#include "ioapic.h"
#include "kprint.h"

namespace {
    constexpr uint16_t PS2_DATA_PORT    = 0x60;
    constexpr uint16_t PS2_STATUS_PORT  = 0x64;

    constexpr size_t BUFFER_SIZE = 128;
    char g_key_buffer[BUFFER_SIZE];
    volatile size_t g_buf_head = 0;
    volatile size_t g_buf_tail = 0;

    bool g_shift_pressed = false;
    bool g_caps_lock = false;

    // US QWERTY Scancode Set 1 Tables
    const char scancode_ascii_lower[128] = {
        0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };

    const char scancode_ascii_upper[128] = {
        0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
        0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
        '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
    };

    void keyboard_isr(interrupt_frame *frame) {
        while (inb(PS2_STATUS_PORT) & 0x01) {
            uint8_t scancode = inb(PS2_DATA_PORT);

            // Handle Shift keys
            if (scancode == 0x2A || scancode == 0x36) {
                g_shift_pressed = true;
            } else if (scancode == 0xAA || scancode == 0xB6) {
                g_shift_pressed = false;
            } else if (scancode == 0x3A) { // Caps Lock
                g_caps_lock = !g_caps_lock;
            } else if (!(scancode & 0x80)) { // Key press (bit 7 clear)
                if (scancode < 128) {
                    bool upper = g_shift_pressed ^ g_caps_lock;
                    char c = upper ? scancode_ascii_upper[scancode] : scancode_ascii_lower[scancode];

                    if (c != 0) {
                        // Push into circular buffer
                        size_t next_head = (g_buf_head + 1) % BUFFER_SIZE;
                        if (next_head != g_buf_tail) {
                            g_key_buffer[g_buf_head] = c;
                            g_buf_head = next_head;
                        }

                        // Print key directly to screen & serial console
                        kprintf("%c", c);
                    }
                }
            }
        }

        lapic_eoi();
        (void)frame;
    }

    driver g_keyboard_drv_instance = {
        "PS/2 Keyboard Driver",
        driver_type::KEYBOARD,
        keyboard_init,
        keyboard_read,
        nullptr
    };
}

bool keyboard_init(void) {
    // 1. Drain any stale data in PS/2 buffer
    while (inb(PS2_STATUS_PORT) & 0x01) {
        inb(PS2_DATA_PORT);
    }

    // 2. Register ISR on vector 0x21
    idt_register_handler(KEYBOARD_IRQ_VECTOR, keyboard_isr);

    // 3. Unmask and route ISA IRQ 1 on IO-APIC
    ioapic_set_irq(1, KEYBOARD_IRQ_VECTOR, 0, false);

    return true;
}

size_t keyboard_read(void *buffer, size_t count) {
    if (!buffer || count == 0) {
        return 0;
    }

    char *dest = static_cast<char *>(buffer);
    size_t read_bytes = 0;

    while (read_bytes < count && g_buf_tail != g_buf_head) {
        dest[read_bytes++] = g_key_buffer[g_buf_tail];
        g_buf_tail = (g_buf_tail + 1) % BUFFER_SIZE;
    }

    return read_bytes;
}

bool keyboard_has_key(void) {
    return g_buf_head != g_buf_tail;
}

const driver *keyboard_get_driver(void) {
    return &g_keyboard_drv_instance;
}
