#include "fb.h"
#include "font.h"

namespace {
    struct limine_framebuffer *g_fb = nullptr;
    uint32_t g_width = 0;
    uint32_t g_height = 0;
    uint32_t g_pitch = 0;
    uint8_t *g_buffer = nullptr;

    // Console state
    constexpr uint32_t MARGIN_X = 16;
    constexpr uint32_t MARGIN_Y = 16;

    uint32_t g_cursor_x = 0;
    uint32_t g_cursor_y = 0;
    uint32_t g_cols = 0;
    uint32_t g_rows = 0;

    uint32_t g_fg_color = fb_color::FG_DEFAULT;
    uint32_t g_bg_color = fb_color::BG_DARK;

    void fb_scroll(void) {
        if (!g_buffer || g_rows == 0) {
            return;
        }

        const uint32_t char_height_bytes = FONT_HEIGHT * g_pitch;
        const uint32_t scroll_height = (g_rows - 1) * FONT_HEIGHT;
        const uint32_t total_bytes_to_move = scroll_height * g_pitch;

        uint8_t *dest = g_buffer + (MARGIN_Y * g_pitch);
        const uint8_t *src = dest + char_height_bytes;

        // Move rows up using 64-bit qword copies (960 words per scanline)
        uint64_t *d64 = reinterpret_cast<uint64_t *>(dest);
        const uint64_t *s64 = reinterpret_cast<const uint64_t *>(src);
        size_t qwords = total_bytes_to_move / 8;
        for (size_t i = 0; i < qwords; ++i) {
            d64[i] = s64[i];
        }

        // Clear the bottom row
        uint8_t *last_row_start = dest + total_bytes_to_move;
        for (uint32_t y = 0; y < FONT_HEIGHT; ++y) {
            uint32_t *line_pixels = reinterpret_cast<uint32_t *>(last_row_start + (y * g_pitch));
            for (uint32_t x = 0; x < g_width; ++x) {
                line_pixels[x] = g_bg_color;
            }
        }
    }
}

void fb_init(struct limine_framebuffer *fb) {
    if (!fb || !fb->address || fb->bpp != 32) {
        return;
    }

    g_fb = fb;
    g_width = static_cast<uint32_t>(fb->width);
    g_height = static_cast<uint32_t>(fb->height);
    g_pitch = static_cast<uint32_t>(fb->pitch);
    g_buffer = static_cast<uint8_t *>(fb->address);

    g_cols = (g_width > (MARGIN_X * 2)) ? (g_width - (MARGIN_X * 2)) / FONT_WIDTH : 0;
    g_rows = (g_height > (MARGIN_Y * 2)) ? (g_height - (MARGIN_Y * 2)) / FONT_HEIGHT : 0;

    g_cursor_x = 0;
    g_cursor_y = 0;

    fb_clear(g_bg_color);
}

bool fb_is_available(void) {
    return g_buffer != nullptr;
}

struct limine_framebuffer *fb_get_struct(void) {
    return g_fb;
}

void fb_clear(uint32_t color) {
    if (!g_buffer) {
        return;
    }

    for (uint32_t y = 0; y < g_height; ++y) {
        uint32_t *row = reinterpret_cast<uint32_t *>(g_buffer + (y * g_pitch));
        for (uint32_t x = 0; x < g_width; ++x) {
            row[x] = color;
        }
    }
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!g_buffer || x >= g_width || y >= g_height) {
        return;
    }

    uint32_t *pixel = reinterpret_cast<uint32_t *>(g_buffer + (y * g_pitch) + (x * sizeof(uint32_t)));
    *pixel = color;
}

void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    if (!g_buffer) {
        return;
    }

    const uint8_t *glyph = &font8x16[static_cast<uint8_t>(c) * FONT_HEIGHT];

    for (uint32_t row = 0; row < FONT_HEIGHT; ++row) {
        if (y + row >= g_height) {
            break;
        }

        uint8_t line = glyph[row];
        uint32_t *screen_row = reinterpret_cast<uint32_t *>(g_buffer + ((y + row) * g_pitch));

        for (uint32_t col = 0; col < FONT_WIDTH; ++col) {
            if (x + col >= g_width) {
                break;
            }

            bool pixel_on = (line & (0x80 >> col)) != 0;
            screen_row[x + col] = pixel_on ? fg : bg;
        }
    }
}

void fb_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg) {
    if (!g_buffer || !str) {
        return;
    }

    uint32_t cur_x = x;
    while (*str) {
        if (*str == '\n') {
            y += FONT_HEIGHT;
            cur_x = x;
            ++str;
            continue;
        }

        fb_draw_char(cur_x, y, *str, fg, bg);
        cur_x += FONT_WIDTH;
        ++str;
    }
}

void fb_console_set_color(uint32_t fg, uint32_t bg) {
    g_fg_color = fg;
    g_bg_color = bg;
}

void fb_console_clear(void) {
    g_cursor_x = 0;
    g_cursor_y = 0;
    fb_clear(g_bg_color);
}

void fb_console_putc(char c) {
    if (!g_buffer || g_cols == 0 || g_rows == 0) {
        return;
    }

    if (c == '\n') {
        g_cursor_x = 0;
        ++g_cursor_y;
    } else if (c == '\r') {
        g_cursor_x = 0;
    } else if (c == '\t') {
        // Tab alignment (4 spaces)
        g_cursor_x = (g_cursor_x + 4) & ~3U;
    } else {
        uint32_t pixel_x = MARGIN_X + (g_cursor_x * FONT_WIDTH);
        uint32_t pixel_y = MARGIN_Y + (g_cursor_y * FONT_HEIGHT);

        fb_draw_char(pixel_x, pixel_y, c, g_fg_color, g_bg_color);
        ++g_cursor_x;
    }

    if (g_cursor_x >= g_cols) {
        g_cursor_x = 0;
        ++g_cursor_y;
    }

    if (g_cursor_y >= g_rows) {
        fb_scroll();
        g_cursor_y = g_rows - 1;
    }
}

void fb_console_puts(const char *str) {
    if (!str) {
        return;
    }
    while (*str) {
        fb_console_putc(*str++);
    }
}
