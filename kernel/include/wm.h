#ifndef LIXOS_WM_H
#define LIXOS_WM_H

#include <stdint.h>
#include <stdbool.h>
#include "compositor.h"
#include "theme.h"
#include "mouse.h"

constexpr uint32_t MAX_WINDOWS = 16;
constexpr int32_t  TITLEBAR_HEIGHT = 28;

struct Window {
    uint32_t id;
    char     title[64];
    int32_t  x;
    int32_t  y;
    int32_t  w;
    int32_t  h;
    Surface  surface;
    bool     active;
    bool     is_focused;
    bool     is_dragging;
    int32_t  drag_offset_x;
    int32_t  drag_offset_y;
};

void wm_init(const ThemeConfig &theme = themes::WINDOWS_10_DARK);
Window* wm_create_window(const char *title, int32_t x, int32_t y, int32_t w, int32_t h);
void wm_destroy_window(Window *win);
void wm_focus_window(Window *win);
void wm_set_theme(const ThemeConfig &theme);
const ThemeConfig& wm_get_theme(void);

// Surface Drawing Utilities
void wm_fill_rect_surface(Surface *surf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void wm_draw_rect_surface(Surface *surf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void wm_draw_string_surface(Surface *surf, int32_t x, int32_t y, const char *str, uint32_t fg, uint32_t bg = 0, bool transparent = true);

// Render and Input Dispatch
void wm_render(void);
void wm_update(const MouseState &mouse);

#endif // LIXOS_WM_H
