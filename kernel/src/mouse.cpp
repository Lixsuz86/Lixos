#include "mouse.h"
#include "io.h"
#include "idt.h"
#include "apic.h"
#include "ioapic.h"
#include "compositor.h"
#include "kprint.h"

namespace {
    constexpr uint16_t PS2_DATA_PORT   = 0x60;
    constexpr uint16_t PS2_STATUS_PORT = 0x64;
    constexpr uint8_t  MOUSE_IRQ_VECTOR = 0x2C; // 32 + 12

    MouseState g_state = { 400, 300, false, false, false };

    uint8_t g_mouse_cycle = 0;
    uint8_t g_mouse_bytes[3] = { 0, 0, 0 };

    void mouse_wait_write(void) {
        int timeout = 100000;
        while ((inb(PS2_STATUS_PORT) & 0x02) && --timeout > 0) {
            asm volatile("pause");
        }
    }

    void mouse_wait_read(void) {
        int timeout = 100000;
        while (!(inb(PS2_STATUS_PORT) & 0x01) && --timeout > 0) {
            asm volatile("pause");
        }
    }

    void mouse_write_cmd(uint8_t cmd) {
        mouse_wait_write();
        outb(PS2_STATUS_PORT, 0xD4); // Tell controller next byte goes to mouse
        mouse_wait_write();
        outb(PS2_DATA_PORT, cmd);
    }

    uint8_t mouse_read(void) {
        mouse_wait_read();
        return inb(PS2_DATA_PORT);
    }

    void mouse_isr(interrupt_frame *frame) {
        (void)frame;

        while (inb(PS2_STATUS_PORT) & 0x01) {
            uint8_t status = inb(PS2_STATUS_PORT);
            uint8_t data = inb(PS2_DATA_PORT);

            // Verify this data came from the auxiliary device (mouse, bit 5 set)
            if (!(status & 0x20)) {
                continue;
            }

            g_mouse_bytes[g_mouse_cycle++] = data;

            if (g_mouse_cycle == 3) {
                g_mouse_cycle = 0;

                uint8_t flags = g_mouse_bytes[0];
                // Byte 0 bit 3 must be 1 in standard PS/2 mouse packets
                if (!(flags & 0x08)) {
                    continue; // Out of sync, skip packet
                }

                int32_t dx = static_cast<int32_t>(g_mouse_bytes[1]);
                int32_t dy = static_cast<int32_t>(g_mouse_bytes[2]);

                if (flags & 0x10) dx |= ~0xFF; // X sign bit
                if (flags & 0x20) dy |= ~0xFF; // Y sign bit

                g_state.left_btn   = (flags & 0x01) != 0;
                g_state.right_btn  = (flags & 0x02) != 0;
                g_state.middle_btn = (flags & 0x04) != 0;

                g_state.x += dx;
                g_state.y -= dy; // Inverted Y in PS/2

                // Screen boundaries clamping
                uint32_t sw = compositor_get_width();
                uint32_t sh = compositor_get_height();
                if (sw > 0 && sh > 0) {
                    if (g_state.x < 0) g_state.x = 0;
                    if (g_state.y < 0) g_state.y = 0;
                    if (g_state.x >= static_cast<int32_t>(sw)) g_state.x = sw - 1;
                    if (g_state.y >= static_cast<int32_t>(sh)) g_state.y = sh - 1;
                }
            }
        }

        lapic_eoi();
    }

    // Modern 12x18 Arrow Cursor Bitmap
    // '.' = Transparent, 'B' = Black Border, 'W' = White Fill
    const char *cursor_bitmap[18] = {
        "B...........",
        "BB..........",
        "BWB.........",
        "BWW B.......",
        "BWWWB.......",
        "BWWWWB......",
        "BWWWWWB.....",
        "BWWWWWWB....",
        "BWWWWWWWB...",
        "BWWWWWWWWB..",
        "BWWWWWWWWWB.",
        "BWWWWBBBBBB.",
        "BWWWB.......",
        "BWBB........",
        "BB.B........",
        "B..B........",
        "...B........",
        "............"
    };
}

void mouse_init(void) {
    // 1. Enable auxiliary mouse port on PS/2 controller
    mouse_wait_write();
    outb(PS2_STATUS_PORT, 0xA8);

    // 2. Read Controller Configuration Byte
    mouse_wait_write();
    outb(PS2_STATUS_PORT, 0x20);
    uint8_t config = mouse_read();

    // Enable IRQ12 (bit 1) and enable mouse clock (clear bit 5)
    config |= 0x02;
    config &= ~0x20;

    mouse_wait_write();
    outb(PS2_STATUS_PORT, 0x60);
    mouse_wait_write();
    outb(PS2_DATA_PORT, config);

    // 3. Reset and enable streaming mode on mouse
    mouse_write_cmd(0xF6); // Set default settings
    mouse_read();          // ACK (0xFA)

    mouse_write_cmd(0xF4); // Enable data reporting
    mouse_read();          // ACK (0xFA)

    // 4. Register ISR and map IRQ12 on IO-APIC
    idt_register_handler(MOUSE_IRQ_VECTOR, mouse_isr);
    ioapic_set_irq(12, MOUSE_IRQ_VECTOR, 0, false);

    // Initial center position
    uint32_t sw = compositor_get_width();
    uint32_t sh = compositor_get_height();
    g_state.x = (sw > 0) ? sw / 2 : 400;
    g_state.y = (sh > 0) ? sh / 2 : 300;

    kprintf("[ OK ] PS/2 Mouse Driver Initialized: IRQ 12 -> Vector 0x%02x, Pos (%d, %d)\n",
            MOUSE_IRQ_VECTOR, g_state.x, g_state.y);
}

MouseState mouse_get_state(void) {
    return g_state;
}

void mouse_set_position(int32_t x, int32_t y) {
    g_state.x = x;
    g_state.y = y;
}

void mouse_draw_cursor(int32_t x, int32_t y) {
    constexpr uint32_t COLOR_BORDER = 0xFF000000;
    constexpr uint32_t COLOR_FILL   = 0xFFFFFFFF;

    for (int row = 0; row < 18; ++row) {
        for (int col = 0; col < 12; ++col) {
            char pixel = cursor_bitmap[row][col];
            if (pixel == 'B') {
                compositor_fill_rect(x + col, y + row, 1, 1, COLOR_BORDER);
            } else if (pixel == 'W') {
                compositor_fill_rect(x + col, y + row, 1, 1, COLOR_FILL);
            }
        }
    }
}
