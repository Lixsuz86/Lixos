#include "wm.h"
#include "compositor.h"
#include "theme.h"
#include "mouse.h"
#include "font.h"
#include "pmm.h"
#include "vmm.h"
#include "string.h"
#include "kprint.h"

namespace {
    Window g_windows[MAX_WINDOWS];
    Window *g_z_order[MAX_WINDOWS];
    size_t g_window_count = 0;
    ThemeConfig g_theme = themes::WINDOWS_10_DARK;

    Window *g_dragged_window = nullptr;
    int32_t g_prev_mouse_x = -1;
    int32_t g_prev_mouse_y = -1;
}

void wm_init(const ThemeConfig &theme) {
    g_theme = theme;
    g_window_count = 0;
    g_dragged_window = nullptr;

    for (size_t i = 0; i < MAX_WINDOWS; ++i) {
        g_windows[i].active = false;
        g_windows[i].id = i + 1;
        g_z_order[i] = nullptr;
    }

    kprintf("[ OK ] Window Manager Initialized (Layout: %s, Theme: '%s')\n",
            (g_theme.layout == DesktopLayout::WINDOWS_10) ? "Windows 10 Classic" : "macOS-esque GNOME Dock",
            g_theme.name);
}

void wm_set_theme(const ThemeConfig &theme) {
    g_theme = theme;
    compositor_damage_all();
}

const ThemeConfig& wm_get_theme(void) {
    return g_theme;
}

void wm_focus_window(Window *win) {
    if (!win || !win->active) return;

    // Find in z-order
    size_t win_idx = 0;
    bool found = false;
    for (size_t i = 0; i < g_window_count; ++i) {
        if (g_z_order[i] == win) {
            win_idx = i;
            found = true;
            break;
        }
    }

    if (!found) return;

    // Shift other windows down and move win to top
    for (size_t i = win_idx; i < g_window_count - 1; ++i) {
        g_z_order[i] = g_z_order[i + 1];
        g_z_order[i]->is_focused = false;
    }

    g_z_order[g_window_count - 1] = win;
    win->is_focused = true;
    compositor_damage_all();
}

Window* wm_create_window(const char *title, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (g_window_count >= MAX_WINDOWS) {
        kprintf("[WM] Error: Maximum windows limit reached!\n");
        return nullptr;
    }

    Window *win = nullptr;
    for (size_t i = 0; i < MAX_WINDOWS; ++i) {
        if (!g_windows[i].active) {
            win = &g_windows[i];
            break;
        }
    }

    if (!win) return nullptr;

    win->active = true;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->is_dragging = false;
    win->is_focused = true;

    const char *t = (title != nullptr) ? title : "Window";
    strncpy(win->title, t, sizeof(win->title) - 1);
    win->title[sizeof(win->title) - 1] = '\0';

    // Allocate 32-bit pixel buffer for window surface
    size_t surf_bytes = w * h * sizeof(uint32_t);
    size_t page_count = (surf_bytes + 4095) / 4096;
    uint64_t phys = pmm_alloc_pages(page_count);
    if (!phys) {
        kprintf("[WM] Error: Failed to allocate window surface memory!\n");
        win->active = false;
        return nullptr;
    }

    win->surface.pixels = reinterpret_cast<uint32_t *>(phys + vmm_get_hhdm_offset());
    win->surface.width = w;
    win->surface.height = h;
    win->surface.pitch = w * sizeof(uint32_t);

    // Fill surface body with theme window background
    for (int32_t py = 0; py < h; ++py) {
        for (int32_t px = 0; px < w; ++px) {
            win->surface.pixels[py * w + px] = g_theme.window_bg;
        }
    }

    g_z_order[g_window_count] = win;
    g_window_count++;

    wm_focus_window(win);
    compositor_damage_all();

    kprintf("[ OK ] Window Created: ID %u ('%s') at (%d, %d), size %dx%d\n",
            win->id, win->title, x, y, w, h);
    return win;
}

void wm_destroy_window(Window *win) {
    if (!win || !win->active) return;

    win->active = false;

    // Free surface pages
    size_t surf_bytes = win->w * win->h * sizeof(uint32_t);
    size_t page_count = (surf_bytes + 4095) / 4096;
    uint64_t phys = reinterpret_cast<uint64_t>(win->surface.pixels) - vmm_get_hhdm_offset();
    pmm_free_pages(phys, page_count);

    // Remove from z-order
    size_t remove_idx = 0;
    for (size_t i = 0; i < g_window_count; ++i) {
        if (g_z_order[i] == win) {
            remove_idx = i;
            break;
        }
    }

    for (size_t i = remove_idx; i < g_window_count - 1; ++i) {
        g_z_order[i] = g_z_order[i + 1];
    }
    g_window_count--;
    g_z_order[g_window_count] = nullptr;

    if (g_window_count > 0) {
        wm_focus_window(g_z_order[g_window_count - 1]);
    }

    compositor_damage_all();
}

static void render_windows_10_taskbar(int32_t sw, int32_t sh) {
    int32_t bar_h = 40;
    int32_t bar_y = sh - bar_h;

    // 1. Taskbar background
    compositor_fill_rect(0, bar_y, sw, bar_h, g_theme.panel_bg);
    compositor_fill_rect(0, bar_y, sw, 1, 0xFF333333); // Top subtle accent line

    // 2. Windows Start Button (Blue 4-tile logo)
    int32_t start_btn_w = 48;
    compositor_fill_rect(0, bar_y + 1, start_btn_w, bar_h - 1, 0xFF252525);

    // Draw modern 4-tile Windows logo
    int32_t logo_x = 16;
    int32_t logo_y = bar_y + 12;
    uint32_t logo_col = g_theme.accent_color;
    compositor_fill_rect(logo_x, logo_y, 7, 7, logo_col);
    compositor_fill_rect(logo_x + 9, logo_y, 7, 7, logo_col);
    compositor_fill_rect(logo_x, logo_y + 9, 7, 7, logo_col);
    compositor_fill_rect(logo_x + 9, logo_y + 9, 7, 7, logo_col);

    // 3. Window Tabs
    int32_t tab_x = 56;
    for (size_t i = 0; i < g_window_count; ++i) {
        Window *w = g_z_order[i];
        if (!w || !w->active) continue;

        int32_t tab_w = 160;
        uint32_t tab_bg = w->is_focused ? 0xFF383838 : 0xFF2A2A2A;
        compositor_fill_rect(tab_x, bar_y + 4, tab_w, bar_h - 8, tab_bg);

        // Active indicator line
        if (w->is_focused) {
            compositor_fill_rect(tab_x, bar_y + bar_h - 2, tab_w, 2, g_theme.accent_color);
        }

        // Window title in tab
        compositor_draw_string(tab_x + 8, bar_y + 12, w->title, 0xFFFFFFFF, 0, true);
        tab_x += tab_w + 4;
    }

    // 4. System Tray (Clock & Status)
    int32_t tray_x = sw - 140;
    compositor_draw_string(tray_x, bar_y + 12, "12:00 PM  LixOS", 0xFFD1D5DB, 0, true);
}

static void render_macos_gnome_layout(int32_t sw, int32_t sh) {
    // 1. Top Bar (height 28)
    int32_t top_h = 28;
    compositor_fill_rect(0, 0, sw, top_h, g_theme.panel_bg);
    compositor_fill_rect(0, top_h - 1, sw, 1, 0xFF374151);

    // Left brand logo & menus
    compositor_draw_string(14, 6, "LixOS", 0xFFFFFFFF, 0, true);
    compositor_draw_string(68, 6, "File", 0xFFD1D5DB, 0, true);
    compositor_draw_string(114, 6, "Edit", 0xFFD1D5DB, 0, true);
    compositor_draw_string(160, 6, "View", 0xFFD1D5DB, 0, true);
    compositor_draw_string(206, 6, "Window", 0xFFD1D5DB, 0, true);
    compositor_draw_string(268, 6, "Help", 0xFFD1D5DB, 0, true);

    // Right status tray
    compositor_draw_string(sw - 180, 6, "100%  Oct 11  12:00 PM", 0xFFE5E7EB, 0, true);

    // 2. Bottom Floating Rounded Dock
    int32_t dock_w = 340;
    int32_t dock_h = 58;
    int32_t dock_x = (sw - dock_w) / 2;
    int32_t dock_y = sh - dock_h - 12;

    // Semi-transparent rounded dock panel
    compositor_fill_rect(dock_x, dock_y, dock_w, dock_h, 0xCC1E293B);
    compositor_draw_rect(dock_x, dock_y, dock_w, dock_h, 0xFF475569);

    // Modern Dock App Icons
    const char *dock_apps[] = { "Files", "Terminal", "Settings", "Browser", "Editor" };
    uint32_t dock_colors[] = { 0xFF0284C7, 0xFF10B981, 0xFFF59E0B, 0xFF6366F1, 0xFFEC4899 };

    for (int i = 0; i < 5; ++i) {
        int32_t icon_x = dock_x + 16 + (i * 64);
        int32_t icon_y = dock_y + 8;
        compositor_fill_rect(icon_x, icon_y, 42, 42, dock_colors[i]);
        compositor_draw_rect(icon_x, icon_y, 42, 42, 0xFFFFFFFF);
        // Draw icon app letter
        compositor_draw_char(icon_x + 17, icon_y + 13, dock_apps[i][0], 0xFFFFFFFF, 0, true);
        // Indicator dot below icon for open apps
        compositor_fill_rect(icon_x + 18, dock_y + dock_h - 5, 6, 3, 0xFFFFFFFF);
    }
}

void wm_render(void) {
    if (!compositor_is_available()) return;

    int32_t sw = static_cast<int32_t>(compositor_get_width());
    int32_t sh = static_cast<int32_t>(compositor_get_height());

    // 1. Render Desktop Wallpaper / Background
    compositor_fill_rect(0, 0, sw, sh, g_theme.desktop_bg);

    // Desktop subtle brand watermark
    compositor_draw_string(sw - 360, sh - 60, "LixOS v2.0 Enterprise Desktop", 0x40FFFFFF, 0, true);

    // 2. Render Windows in Z-Order
    for (size_t i = 0; i < g_window_count; ++i) {
        Window *w = g_z_order[i];
        if (!w || !w->active) continue;

        // Window drop shadow (alpha blended)
        compositor_fill_rect(w->x - 4, w->y - TITLEBAR_HEIGHT - 4, w->w + 8, w->h + TITLEBAR_HEIGHT + 8, 0x33000000);

        // Window Titlebar
        uint32_t tb_bg = w->is_focused ? g_theme.titlebar_active_bg : g_theme.titlebar_inactive_bg;
        compositor_fill_rect(w->x, w->y - TITLEBAR_HEIGHT, w->w, TITLEBAR_HEIGHT, tb_bg);

        // Focused accent line on top
        if (w->is_focused) {
            compositor_fill_rect(w->x, w->y - TITLEBAR_HEIGHT, w->w, 2, g_theme.accent_color);
        }

        // Title text
        compositor_draw_string(w->x + 10, w->y - TITLEBAR_HEIGHT + 7, w->title, g_theme.titlebar_fg, 0, true);

        // Titlebar controls
        if (g_theme.layout == DesktopLayout::WINDOWS_10) {
            // Windows-style right buttons: Minimize [-], Maximize [square], Close [X]
            int32_t btn_y = w->y - TITLEBAR_HEIGHT + 4;
            int32_t btn_h = 20;

            // Close Button [X]
            int32_t close_x = w->x + w->w - 28;
            compositor_fill_rect(close_x, btn_y, 24, btn_h, 0xFFE81123);
            compositor_draw_string(close_x + 8, btn_y + 2, "X", 0xFFFFFFFF, 0, true);

            // Maximize Button [O]
            int32_t max_x = close_x - 28;
            compositor_fill_rect(max_x, btn_y, 24, btn_h, 0xFF3E3E3E);
            compositor_draw_rect(max_x + 6, btn_y + 5, 12, 10, 0xFFCCCCCC);

            // Minimize Button [-]
            int32_t min_x = max_x - 28;
            compositor_fill_rect(min_x, btn_y, 24, btn_h, 0xFF3E3E3E);
            compositor_fill_rect(min_x + 6, btn_y + 10, 12, 2, 0xFFCCCCCC);
        } else {
            // macOS-style traffic light dots: Red (Close), Yellow (Min), Green (Max)
            int32_t dot_y = w->y - TITLEBAR_HEIGHT + 8;
            compositor_fill_rect(w->x + 10, dot_y, 11, 11, 0xFFFF5F56); // Red Close
            compositor_fill_rect(w->x + 26, dot_y, 11, 11, 0xFFFFBD2E); // Yellow Min
            compositor_fill_rect(w->x + 42, dot_y, 11, 11, 0xFF27C93F); // Green Max
        }

        // Window Surface Content
        compositor_draw_surface(&w->surface, w->x, w->y);

        // Window Outer Border
        compositor_draw_rect(w->x, w->y - TITLEBAR_HEIGHT, w->w, w->h + TITLEBAR_HEIGHT, g_theme.border_color);
    }

    // 3. Render Desktop Shell Chrome according to layout
    if (g_theme.layout == DesktopLayout::WINDOWS_10) {
        render_windows_10_taskbar(sw, sh);
    } else {
        render_macos_gnome_layout(sw, sh);
    }

    // 4. Render Mouse Cursor
    MouseState m = mouse_get_state();
    mouse_draw_cursor(m.x, m.y);

    // 5. Present through compositor with damage tracking
    compositor_present();
}

void wm_update(const MouseState &mouse) {
    // 1. Check Dragging
    if (g_dragged_window != nullptr) {
        if (mouse.left_btn) {
            g_dragged_window->x = mouse.x - g_dragged_window->drag_offset_x;
            g_dragged_window->y = mouse.y - g_dragged_window->drag_offset_y;
            compositor_damage_all();
        } else {
            g_dragged_window->is_dragging = false;
            g_dragged_window = nullptr;
        }
    } else if (mouse.left_btn && (g_prev_mouse_x != mouse.x || g_prev_mouse_y != mouse.y)) {
        // Hit-test windows from top to bottom
        for (int32_t i = static_cast<int32_t>(g_window_count) - 1; i >= 0; --i) {
            Window *w = g_z_order[i];
            if (!w || !w->active) continue;

            // Check titlebar hit
            if (mouse.x >= w->x && mouse.x < w->x + w->w &&
                mouse.y >= w->y - TITLEBAR_HEIGHT && mouse.y < w->y) {
                
                // Check if close button was clicked (Windows layout)
                if (g_theme.layout == DesktopLayout::WINDOWS_10 && mouse.x >= w->x + w->w - 28) {
                    wm_destroy_window(w);
                    break;
                }

                wm_focus_window(w);
                w->is_dragging = true;
                w->drag_offset_x = mouse.x - w->x;
                w->drag_offset_y = mouse.y - w->y;
                g_dragged_window = w;
                break;
            }

            // Check window body hit
            if (mouse.x >= w->x && mouse.x < w->x + w->w &&
                mouse.y >= w->y && mouse.y < w->y + w->h) {
                wm_focus_window(w);
                break;
            }
        }
    }

    g_prev_mouse_x = mouse.x;
    g_prev_mouse_y = mouse.y;

    wm_render();
}

void wm_fill_rect_surface(Surface *surf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (!surf || !surf->pixels || w <= 0 || h <= 0) return;

    int32_t x1 = (x < 0) ? 0 : x;
    int32_t y1 = (y < 0) ? 0 : y;
    int32_t x2 = (x + w > static_cast<int32_t>(surf->width)) ? static_cast<int32_t>(surf->width) : x + w;
    int32_t y2 = (y + h > static_cast<int32_t>(surf->height)) ? static_cast<int32_t>(surf->height) : y + h;

    if (x1 >= x2 || y1 >= y2) return;

    uint32_t stride = surf->pitch / 4;
    for (int32_t py = y1; py < y2; ++py) {
        uint32_t *row = surf->pixels + (py * stride);
        for (int32_t px = x1; px < x2; ++px) {
            row[px] = color;
        }
    }
}

void wm_draw_rect_surface(Surface *surf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    wm_fill_rect_surface(surf, x, y, w, 1, color);
    wm_fill_rect_surface(surf, x, y + h - 1, w, 1, color);
    wm_fill_rect_surface(surf, x, y, 1, h, color);
    wm_fill_rect_surface(surf, x + w - 1, y, 1, h, color);
}

void wm_draw_string_surface(Surface *surf, int32_t x, int32_t y, const char *str, uint32_t fg, uint32_t bg, bool transparent) {
    if (!surf || !surf->pixels || !str) return;

    uint32_t stride = surf->pitch / 4;
    int32_t cur_x = x;
    int32_t cur_y = y;

    while (*str) {
        if (*str == '\n') {
            cur_y += FONT_HEIGHT;
            cur_x = x;
        } else {
            char c = *str;
            if (c >= 0 && c <= 127) {
                const uint8_t *glyph = &font8x16[static_cast<uint8_t>(c) * FONT_HEIGHT];
                for (uint32_t r = 0; r < FONT_HEIGHT; ++r) {
                    int32_t py = cur_y + r;
                    if (py < 0 || py >= static_cast<int32_t>(surf->height)) continue;

                    uint8_t bits = glyph[r];
                    uint32_t *row = surf->pixels + (py * stride);

                    for (uint32_t col = 0; col < FONT_WIDTH; ++col) {
                        int32_t px = cur_x + col;
                        if (px < 0 || px >= static_cast<int32_t>(surf->width)) continue;

                        if (bits & (0x80 >> col)) {
                            row[px] = fg;
                        } else if (!transparent) {
                            row[px] = bg;
                        }
                    }
                }
            }
            cur_x += FONT_WIDTH;
        }
        str++;
    }
}
