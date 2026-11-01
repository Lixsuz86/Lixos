#ifndef LIXOS_FB_H
#define LIXOS_FB_H

#include "limine.h"
#include <stdint.h>
#include <stddef.h>

// Curated 32-bit RGB Color Palette (ARGB8888)
namespace fb_color {
    constexpr uint32_t BLACK       = 0x00000000;
    constexpr uint32_t BG_DARK     = 0x000D1117; // Modern sleek dark background
    constexpr uint32_t FG_DEFAULT  = 0x00C9D1D9; // Clean light-gray font color
    constexpr uint32_t WHITE       = 0x00FFFFFF;
    constexpr uint32_t BLUE        = 0x0058A6FF;
    constexpr uint32_t GREEN       = 0x003FB950;
    constexpr uint32_t RED         = 0x00F85149;
    constexpr uint32_t YELLOW      = 0x00D29922;
    constexpr uint32_t PURPLE      = 0x00BC8CFF;
    constexpr uint32_t CYAN        = 0x0039C5BB;
    constexpr uint32_t GRAY        = 0x006E7681;
}

// Initialize Framebuffer and Console
void fb_init(struct limine_framebuffer *fb);

// Check if framebuffer is available
bool fb_is_available(void);
struct limine_framebuffer *fb_get_struct(void);

// Basic drawing primitives
void fb_clear(uint32_t color);
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);
void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg);

// Console output primitives (with cursor tracking and scrolling)
void fb_console_set_color(uint32_t fg, uint32_t bg);
void fb_console_putc(char c);
void fb_console_puts(const char *str);
void fb_console_clear(void);

#endif // LIXOS_FB_H
