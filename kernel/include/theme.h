#ifndef LIXOS_THEME_H
#define LIXOS_THEME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum class DesktopLayout : uint32_t {
    WINDOWS_10 = 0, // Bottom taskbar, start button, system tray, right window controls
    MACOS_DOCK = 1  // Top menu bar, bottom-center floating rounded dock, left window controls
};

struct ThemeConfig {
    char          name[32];
    uint32_t      accent_color;
    uint32_t      desktop_bg;
    uint32_t      window_bg;
    uint32_t      titlebar_active_bg;
    uint32_t      titlebar_inactive_bg;
    uint32_t      titlebar_fg;
    uint32_t      panel_bg;
    uint32_t      border_color;
    DesktopLayout layout;
};

// Available Preset Themes
namespace themes {
    // 1. Windows 10 Dark Modern (Default)
    constexpr ThemeConfig WINDOWS_10_DARK = {
        "Windows 10 Dark",
        0xFF0078D4, // Windows Blue
        0xFF0D1117, // Deep Dark Desktop
        0xFF1E1E1E, // Dark Gray Window Body
        0xFF2D2D2D, // Active Titlebar
        0xFF202020, // Inactive Titlebar
        0xFFFFFFFF, // White Title Text
        0xFF1F1F1F, // Taskbar Dark
        0xFF333333, // Window Border
        DesktopLayout::WINDOWS_10
    };

    // 2. macOS Monterey Style (Second Choice)
    constexpr ThemeConfig MACOS_DARK = {
        "macOS Monterey Dark",
        0xFF388BFD, // macOS Blue
        0xFF111827, // Slate Navy Desktop
        0xFF1F2937, // Window Body
        0xFF283548, // Header Bar
        0xFF1E2633, // Inactive Header
        0xFFF3F4F6, // Title Text
        0xE61E293B, // Semi-transparent Topbar/Dock
        0xFF374151, // Window Border
        DesktopLayout::MACOS_DOCK
    };

    // 3. Cyberpunk Gold
    constexpr ThemeConfig CYBERPUNK_GOLD = {
        "Cyberpunk Gold",
        0xFFFFBE0B, // Neon Gold
        0xFF080708, // Pitch Black
        0xFF141414, // Dark Gray
        0xFF262626, // Titlebar
        0xFF181818, // Inactive
        0xFFFFBE0B, // Gold Text
        0xFF1A1A1A, // Panel
        0xFFFFBE0B, // Border
        DesktopLayout::WINDOWS_10
    };
}

// =============================================================================
// Milestone 11.5: Shareable Theme Packages (.lixtheme) Specification & APIs
// =============================================================================

constexpr char     LIXTHEME_MAGIC[8] = {'L', 'I', 'X', 'T', 'H', 'M', '0', '1'};
constexpr uint32_t LIXTHEME_VERSION = 1;
constexpr size_t   LIXTHEME_MAX_ASSETS = 8;
constexpr size_t   LIXTHEME_MAX_PKG_SIZE = 128 * 1024; // 128 KB max package size

enum class LixAssetType : uint32_t {
    BITMAP_FONT  = 1,   // 8x16 font table (4096 bytes)
    ICON_ARGB    = 2,   // ARGB icon data
    CURSOR_ARGB  = 3    // ARGB cursor
};

struct __attribute__((packed)) LixThemeAssetEntry {
    char         name[32];     // Asset identifier e.g. "font8x16"
    uint32_t     type;         // LixAssetType
    uint32_t     offset;       // Byte offset from start of package
    uint32_t     size;         // Size of asset in bytes
};

struct __attribute__((packed)) LixThemeHeader {
    char     magic[8];              // "LIXTHM01"
    char     name[32];              // Display name e.g. "Cyberpunk Gold"
    char     author[32];            // Theme creator / author
    uint32_t version;               // 1
    uint32_t layout;                // 0 = Windows 10, 1 = macOS Dock
    uint32_t accent_color;          // ARGB
    uint32_t desktop_bg;            // ARGB
    uint32_t window_bg;             // ARGB
    uint32_t titlebar_active_bg;    // ARGB
    uint32_t titlebar_inactive_bg;  // ARGB
    uint32_t titlebar_fg;           // ARGB
    uint32_t panel_bg;              // ARGB
    uint32_t border_color;          // ARGB
    uint32_t asset_count;           // Number of embedded assets (<= LIXTHEME_MAX_ASSETS)
    uint32_t total_size;            // Total size of package in bytes
    uint32_t checksum;              // Additive 32-bit checksum of payload
};

// Security validator: Verifies package contains ONLY passive data and no executable code/scripts
bool theme_validate_security(const uint8_t *data, size_t size, const char **out_reason);

// Export theme configuration and bundled assets to a .lixtheme file on VFS
int theme_export_package(const char *filepath, const ThemeConfig &theme, const char *author, bool bundle_font = true);

// Import and validate .lixtheme package from VFS with passive-data security enforcement
int theme_import_package(const char *filepath, ThemeConfig *out_theme);

// Import and apply to Window Manager with automatic safe fallback on failure
bool theme_apply_package(const char *filepath);

#endif // LIXOS_THEME_H
