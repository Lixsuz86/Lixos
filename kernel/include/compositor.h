#ifndef LIXOS_COMPOSITOR_H
#define LIXOS_COMPOSITOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// 2D Geometric Rectangle
struct Rect {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
};

// Independent Graphic Surface
struct Surface {
    uint32_t *pixels;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch; // bytes per scanline
};

// Color manipulation utilities (ARGB8888)
namespace color {
    constexpr uint32_t make_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8)  |
               static_cast<uint32_t>(b);
    }

    constexpr uint8_t get_a(uint32_t c) { return (c >> 24) & 0xFF; }
    constexpr uint8_t get_r(uint32_t c) { return (c >> 16) & 0xFF; }
    constexpr uint8_t get_g(uint32_t c) { return (c >> 8)  & 0xFF; }
    constexpr uint8_t get_b(uint32_t c) { return c & 0xFF; }

    inline uint32_t blend(uint32_t bg, uint32_t fg) {
        uint32_t alpha = get_a(fg);
        if (alpha == 255) return fg;
        if (alpha == 0)   return bg;

        uint32_t inv_alpha = 255 - alpha;
        uint32_t r = (get_r(fg) * alpha + get_r(bg) * inv_alpha) / 255;
        uint32_t g = (get_g(fg) * alpha + get_g(bg) * inv_alpha) / 255;
        uint32_t b = (get_b(fg) * alpha + get_b(bg) * inv_alpha) / 255;

        return make_argb(255, static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
    }
}

// Core Compositor Management APIs
void compositor_init(void);
bool compositor_is_available(void);
uint32_t compositor_get_width(void);
uint32_t compositor_get_height(void);
uint32_t* compositor_get_backbuffer(void);

// Damage tracking & presentation
void compositor_damage(int32_t x, int32_t y, int32_t w, int32_t h);
void compositor_damage_all(void);
void compositor_present(void);

// Drawing primitives into backbuffer
void compositor_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void compositor_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void compositor_draw_surface(const Surface *surf, int32_t dest_x, int32_t dest_y, const Rect *clip = nullptr);
void compositor_draw_char(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg = 0, bool transparent = true);
void compositor_draw_string(int32_t x, int32_t y, const char *str, uint32_t fg, uint32_t bg = 0, bool transparent = true);

#endif // LIXOS_COMPOSITOR_H
