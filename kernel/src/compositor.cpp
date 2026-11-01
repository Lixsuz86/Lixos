#include "compositor.h"
#include "fb.h"
#include "font.h"
#include "pmm.h"
#include "vmm.h"
#include "string.h"
#include "kprint.h"

namespace {
    uint32_t *g_hw_buffer = nullptr;
    uint32_t *g_backbuffer = nullptr;
    uint32_t  g_width = 0;
    uint32_t  g_height = 0;
    uint32_t  g_pitch = 0; // bytes per line

    // Damage tracking bounding box
    int32_t g_dmg_x1 = 0;
    int32_t g_dmg_y1 = 0;
    int32_t g_dmg_x2 = -1;
    int32_t g_dmg_y2 = -1;
    bool    g_has_damage = false;

    inline void expand_damage(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 >= static_cast<int32_t>(g_width))  x2 = g_width - 1;
        if (y2 >= static_cast<int32_t>(g_height)) y2 = g_height - 1;

        if (x1 > x2 || y1 > y2) return;

        if (!g_has_damage) {
            g_dmg_x1 = x1;
            g_dmg_y1 = y1;
            g_dmg_x2 = x2;
            g_dmg_y2 = y2;
            g_has_damage = true;
        } else {
            if (x1 < g_dmg_x1) g_dmg_x1 = x1;
            if (y1 < g_dmg_y1) g_dmg_y1 = y1;
            if (x2 > g_dmg_x2) g_dmg_x2 = x2;
            if (y2 > g_dmg_y2) g_dmg_y2 = y2;
        }
    }
}

void compositor_init(void) {
    struct limine_framebuffer *fb = fb_get_struct();
    if (!fb || !fb->address || fb->bpp != 32) {
        kprintf("[COMPOSITOR] Error: No 32bpp framebuffer available!\n");
        return;
    }

    g_width = static_cast<uint32_t>(fb->width);
    g_height = static_cast<uint32_t>(fb->height);
    g_pitch = static_cast<uint32_t>(fb->pitch);
    g_hw_buffer = static_cast<uint32_t *>(fb->address);

    size_t total_bytes = g_height * g_pitch;
    size_t page_count = (total_bytes + 4095) / 4096;

    uint64_t phys = pmm_alloc_pages(page_count);
    if (!phys) {
        kprintf("[COMPOSITOR] Error: Failed to allocate %u KB for backbuffer!\n",
                static_cast<uint32_t>(total_bytes / 1024));
        return;
    }

    g_backbuffer = reinterpret_cast<uint32_t *>(phys + vmm_get_hhdm_offset());
    memset(g_backbuffer, 0, total_bytes);

    compositor_damage_all();
    kprintf("[ OK ] Compositor Initialized: %ux%u (Pitch %u bytes, Double Buffering Active)\n",
            g_width, g_height, g_pitch);
}

bool compositor_is_available(void) {
    return (g_hw_buffer != nullptr && g_backbuffer != nullptr);
}

uint32_t compositor_get_width(void) {
    return g_width;
}

uint32_t compositor_get_height(void) {
    return g_height;
}

uint32_t* compositor_get_backbuffer(void) {
    return g_backbuffer;
}

void compositor_damage(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (w <= 0 || h <= 0) return;
    expand_damage(x, y, x + w - 1, y + h - 1);
}

void compositor_damage_all(void) {
    expand_damage(0, 0, g_width - 1, g_height - 1);
}

void compositor_present(void) {
    if (!compositor_is_available() || !g_has_damage) {
        return;
    }

    uint32_t pixels_per_pitch = g_pitch / 4;

    for (int32_t y = g_dmg_y1; y <= g_dmg_y2; ++y) {
        uint32_t *src_line = g_backbuffer + (y * pixels_per_pitch) + g_dmg_x1;
        uint32_t *dst_line = g_hw_buffer   + (y * pixels_per_pitch) + g_dmg_x1;
        size_t copy_pixels = g_dmg_x2 - g_dmg_x1 + 1;

        memcpy(dst_line, src_line, copy_pixels * sizeof(uint32_t));
    }

    g_has_damage = false;
}

void compositor_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    if (!g_backbuffer || w <= 0 || h <= 0) return;

    int32_t x1 = (x < 0) ? 0 : x;
    int32_t y1 = (y < 0) ? 0 : y;
    int32_t x2 = (x + w > static_cast<int32_t>(g_width)) ? static_cast<int32_t>(g_width) : x + w;
    int32_t y2 = (y + h > static_cast<int32_t>(g_height)) ? static_cast<int32_t>(g_height) : y + h;

    if (x1 >= x2 || y1 >= y2) return;

    uint32_t pixels_per_pitch = g_pitch / 4;
    uint8_t alpha = color::get_a(c);

    if (alpha == 255) {
        // Fully opaque fast fill
        for (int32_t py = y1; py < y2; ++py) {
            uint32_t *row = g_backbuffer + (py * pixels_per_pitch);
            for (int32_t px = x1; px < x2; ++px) {
                row[px] = c;
            }
        }
    } else if (alpha > 0) {
        // Alpha blended fill
        for (int32_t py = y1; py < y2; ++py) {
            uint32_t *row = g_backbuffer + (py * pixels_per_pitch);
            for (int32_t px = x1; px < x2; ++px) {
                row[px] = color::blend(row[px], c);
            }
        }
    }

    expand_damage(x1, y1, x2 - 1, y2 - 1);
}

void compositor_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    compositor_fill_rect(x, y, w, 1, color);             // Top
    compositor_fill_rect(x, y + h - 1, w, 1, color);     // Bottom
    compositor_fill_rect(x, y, 1, h, color);             // Left
    compositor_fill_rect(x + w - 1, y, 1, h, color);     // Right
}

void compositor_draw_surface(const Surface *surf, int32_t dest_x, int32_t dest_y, const Rect *clip) {
    if (!g_backbuffer || !surf || !surf->pixels) return;

    int32_t clip_x1 = 0;
    int32_t clip_y1 = 0;
    int32_t clip_x2 = g_width;
    int32_t clip_y2 = g_height;

    if (clip) {
        if (clip->x > clip_x1) clip_x1 = clip->x;
        if (clip->y > clip_y1) clip_y1 = clip->y;
        if (clip->x + clip->w < clip_x2) clip_x2 = clip->x + clip->w;
        if (clip->y + clip->h < clip_y2) clip_y2 = clip->y + clip->h;
    }

    uint32_t pixels_per_pitch = g_pitch / 4;
    uint32_t surf_pixels_per_pitch = surf->pitch / 4;

    int32_t draw_x1 = (dest_x < clip_x1) ? clip_x1 : dest_x;
    int32_t draw_y1 = (dest_y < clip_y1) ? clip_y1 : dest_y;
    int32_t draw_x2 = (dest_x + static_cast<int32_t>(surf->width) > clip_x2) ? clip_x2 : dest_x + surf->width;
    int32_t draw_y2 = (dest_y + static_cast<int32_t>(surf->height) > clip_y2) ? clip_y2 : dest_y + surf->height;

    if (draw_x1 >= draw_x2 || draw_y1 >= draw_y2) return;

    for (int32_t dy = draw_y1; dy < draw_y2; ++dy) {
        int32_t sy = dy - dest_y;
        uint32_t *dst_row = g_backbuffer + (dy * pixels_per_pitch);
        const uint32_t *src_row = surf->pixels + (sy * surf_pixels_per_pitch);

        for (int32_t dx = draw_x1; dx < draw_x2; ++dx) {
            int32_t sx = dx - dest_x;
            uint32_t pixel = src_row[sx];
            dst_row[dx] = color::blend(dst_row[dx], pixel);
        }
    }

    expand_damage(draw_x1, draw_y1, draw_x2 - 1, draw_y2 - 1);
}

void compositor_draw_char(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg, bool transparent) {
    if (!g_backbuffer) return;
    if (c < 0 || c > 127) return;

    const uint8_t *glyph = &font8x16[static_cast<uint8_t>(c) * FONT_HEIGHT];
    uint32_t pixels_per_pitch = g_pitch / 4;

    for (uint32_t row = 0; row < FONT_HEIGHT; ++row) {
        int32_t py = y + row;
        if (py < 0 || py >= static_cast<int32_t>(g_height)) continue;

        uint8_t bits = glyph[row];
        uint32_t *row_ptr = g_backbuffer + (py * pixels_per_pitch);

        for (uint32_t col = 0; col < FONT_WIDTH; ++col) {
            int32_t px = x + col;
            if (px < 0 || px >= static_cast<int32_t>(g_width)) continue;

            if (bits & (0x80 >> col)) {
                row_ptr[px] = fg;
            } else if (!transparent) {
                row_ptr[px] = bg;
            }
        }
    }

    expand_damage(x, y, x + FONT_WIDTH - 1, y + FONT_HEIGHT - 1);
}

void compositor_draw_string(int32_t x, int32_t y, const char *str, uint32_t fg, uint32_t bg, bool transparent) {
    if (!str) return;
    int32_t cur_x = x;
    while (*str) {
        if (*str == '\n') {
            y += FONT_HEIGHT;
            cur_x = x;
        } else {
            compositor_draw_char(cur_x, y, *str, fg, bg, transparent);
            cur_x += FONT_WIDTH;
        }
        str++;
    }
}
