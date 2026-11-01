#include "theme.h"
#include "vfs.h"
#include "heap.h"
#include "string.h"
#include "font.h"
#include "kprint.h"
#include "wm.h"

namespace {
    uint32_t compute_checksum(const uint8_t *data, size_t size) {
        uint32_t sum = 0x55AA55AA;
        for (size_t i = 0; i < size; ++i) {
            sum = (sum << 3) | (sum >> 29);
            sum += data[i];
        }
        return sum;
    }

    bool is_safe_string(const char *str, size_t max_len) {
        if (!str) return false;
        size_t len = 0;
        while (len < max_len && str[len] != '\0') {
            unsigned char c = static_cast<unsigned char>(str[len]);
            // Only printable ASCII characters (32 to 126) allowed
            if (c < 32 || c > 126) {
                return false;
            }
            len++;
        }
        return (len < max_len); // Must be null-terminated within max_len
    }
}

bool theme_validate_security(const uint8_t *data, size_t size, const char **out_reason) {
    if (!data || size < sizeof(LixThemeHeader)) {
        if (out_reason) *out_reason = "Package data too small or null";
        return false;
    }

    if (size > LIXTHEME_MAX_PKG_SIZE) {
        if (out_reason) *out_reason = "Package size exceeds maximum limit (128 KB)";
        return false;
    }

    const auto *header = reinterpret_cast<const LixThemeHeader *>(data);

    // 1. Magic check
    if (memcmp(header->magic, LIXTHEME_MAGIC, 8) != 0) {
        if (out_reason) *out_reason = "Invalid magic identifier (must be 'LIXTHM01')";
        return false;
    }

    // 2. Version check
    if (header->version != LIXTHEME_VERSION) {
        if (out_reason) *out_reason = "Unsupported format version";
        return false;
    }

    // 3. Size check
    if (header->total_size != size) {
        if (out_reason) *out_reason = "Package size mismatch with header total_size";
        return false;
    }

    // 4. Layout check
    if (header->layout > 1) {
        if (out_reason) *out_reason = "Invalid desktop layout identifier";
        return false;
    }

    // 5. String sanitization (Passive text data only)
    if (!is_safe_string(header->name, sizeof(header->name))) {
        if (out_reason) *out_reason = "Theme name contains illegal non-printable characters or is not null-terminated";
        return false;
    }

    if (!is_safe_string(header->author, sizeof(header->author))) {
        if (out_reason) *out_reason = "Theme author contains illegal non-printable characters or is not null-terminated";
        return false;
    }

    // 6. Asset table validation
    if (header->asset_count > LIXTHEME_MAX_ASSETS) {
        if (out_reason) *out_reason = "Asset count exceeds maximum limit (8)";
        return false;
    }

    size_t min_header_size = sizeof(LixThemeHeader) + (header->asset_count * sizeof(LixThemeAssetEntry));
    if (size < min_header_size) {
        if (out_reason) *out_reason = "Package truncated before asset headers";
        return false;
    }

    const auto *assets = reinterpret_cast<const LixThemeAssetEntry *>(data + sizeof(LixThemeHeader));
    for (uint32_t i = 0; i < header->asset_count; ++i) {
        if (!is_safe_string(assets[i].name, sizeof(assets[i].name))) {
            if (out_reason) *out_reason = "Asset name contains illegal non-printable characters";
            return false;
        }

        if (assets[i].offset < min_header_size || (assets[i].offset + assets[i].size) > size) {
            if (out_reason) *out_reason = "Asset offset or size extends outside package boundary";
            return false;
        }

        uint32_t type_val = assets[i].type;
        if (type_val < 1 || type_val > 3) {
            if (out_reason) *out_reason = "Invalid asset type (must be 1: Font, 2: Icon, 3: Cursor)";
            return false;
        }

        // If bitmap font, size must be exactly 4096 bytes (256 * 16)
        if (type_val == 1 && assets[i].size != 4096) {
            if (out_reason) *out_reason = "Bitmap font asset must be exactly 4096 bytes";
            return false;
        }
    }

    // =========================================================================
    // 7. CRITICAL SECURITY RULE: Scan for Executable / Script Signatures
    // =========================================================================
    // A theme package MUST NEVER contain executable code, scripts, or macros.
    for (size_t i = 0; i < size; ++i) {
        // Check for ELF executable signature: 0x7F 'E' 'L' 'F'
        if (i + 4 <= size && data[i] == 0x7F && data[i + 1] == 'E' && data[i + 2] == 'L' && data[i + 3] == 'F') {
            if (out_reason) *out_reason = "SECURITY ALERT: Executable ELF binary signature detected in package payload!";
            return false;
        }

        // Check for Script interpreter shebang: '#' '!'
        if (i + 2 <= size && data[i] == '#' && data[i + 1] == '!') {
            if (out_reason) *out_reason = "SECURITY ALERT: Script interpreter shebang (#!) detected in package payload!";
            return false;
        }

        // Check for MS-DOS / PE executable header: 'M' 'Z' (outside of raw bitmap data)
        if (i < min_header_size && i + 2 <= size && data[i] == 'M' && data[i + 1] == 'Z') {
            if (out_reason) *out_reason = "SECURITY ALERT: PE executable signature detected in package header!";
            return false;
        }
    }

    // 8. Checksum verification
    // Payload checksum is computed from offset 12 (after magic and total_size/checksum) to end of file
    size_t check_start = sizeof(LixThemeHeader);
    uint32_t computed = compute_checksum(data + check_start, size - check_start);
    if (computed != header->checksum) {
        if (out_reason) *out_reason = "Package checksum mismatch (data corruption or unauthorized tampering detected)";
        return false;
    }

    return true;
}

int theme_export_package(const char *filepath, const ThemeConfig &theme, const char *author, bool bundle_font) {
    if (!filepath) return -1;

    uint32_t asset_count = bundle_font ? 1 : 0;
    uint32_t font_size = bundle_font ? sizeof(font8x16) : 0;
    uint32_t total_size = sizeof(LixThemeHeader) + (asset_count * sizeof(LixThemeAssetEntry)) + font_size;

    uint8_t *buf = static_cast<uint8_t *>(kmalloc(total_size));
    if (!buf) return -1;

    memset(buf, 0, total_size);

    auto *header = reinterpret_cast<LixThemeHeader *>(buf);
    memcpy(header->magic, LIXTHEME_MAGIC, 8);
    strncpy(header->name, theme.name, sizeof(header->name) - 1);
    strncpy(header->author, author ? author : "LixOS User", sizeof(header->author) - 1);
    header->version = LIXTHEME_VERSION;
    header->layout = static_cast<uint32_t>(theme.layout);
    header->accent_color = theme.accent_color;
    header->desktop_bg = theme.desktop_bg;
    header->window_bg = theme.window_bg;
    header->titlebar_active_bg = theme.titlebar_active_bg;
    header->titlebar_inactive_bg = theme.titlebar_inactive_bg;
    header->titlebar_fg = theme.titlebar_fg;
    header->panel_bg = theme.panel_bg;
    header->border_color = theme.border_color;
    header->asset_count = asset_count;
    header->total_size = total_size;

    if (bundle_font) {
        auto *assets = reinterpret_cast<LixThemeAssetEntry *>(buf + sizeof(LixThemeHeader));
        strncpy(assets[0].name, "font8x16", sizeof(assets[0].name) - 1);
        assets[0].type = static_cast<uint32_t>(LixAssetType::BITMAP_FONT);
        assets[0].offset = sizeof(LixThemeHeader) + sizeof(LixThemeAssetEntry);
        assets[0].size = font_size;

        // Copy font data into package payload
        memcpy(buf + assets[0].offset, font8x16, font_size);
    }

    // Compute checksum over payload
    size_t check_start = sizeof(LixThemeHeader);
    header->checksum = compute_checksum(buf + check_start, total_size - check_start);

    // Write to VFS
    int fd = vfs_open(filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        kfree(buf);
        return -1;
    }

    int64_t written = vfs_write(fd, buf, total_size);
    vfs_close(fd);
    kfree(buf);

    if (written != static_cast<int64_t>(total_size)) {
        return -2;
    }

    kprintf("[THEME] Exported theme '%s' -> '%s' (%u bytes, Assets: %u)\n",
            theme.name, filepath, static_cast<unsigned int>(total_size), static_cast<unsigned int>(asset_count));
    return 0;
}

int theme_import_package(const char *filepath, ThemeConfig *out_theme) {
    if (!filepath) return -1;

    int fd = vfs_open(filepath, O_RDONLY);
    if (fd < 0) {
        kprintf("[THEME] Failed to open theme package '%s'\n", filepath);
        if (out_theme) *out_theme = themes::WINDOWS_10_DARK;
        return -1;
    }

    uint8_t *buf = static_cast<uint8_t *>(kmalloc(LIXTHEME_MAX_PKG_SIZE));
    if (!buf) {
        vfs_close(fd);
        if (out_theme) *out_theme = themes::WINDOWS_10_DARK;
        return -1;
    }

    int64_t bytes_read = vfs_read(fd, buf, LIXTHEME_MAX_PKG_SIZE);
    vfs_close(fd);

    if (bytes_read <= 0) {
        kfree(buf);
        if (out_theme) *out_theme = themes::WINDOWS_10_DARK;
        return -1;
    }

    const char *reason = nullptr;
    bool valid = theme_validate_security(buf, static_cast<size_t>(bytes_read), &reason);
    if (!valid) {
        kprintf("[SECURITY ALERT] Malicious or invalid theme package in '%s'!\n", filepath);
        kprintf("                 Violation: %s\n", reason ? reason : "Unknown");
        kprintf("[SECURITY] Enforcing safe fallback -> Reverting to default 'Windows 10 Dark' theme.\n");
        if (out_theme) {
            *out_theme = themes::WINDOWS_10_DARK;
        }
        kfree(buf);
        return -2; // Security violation
    }

    const auto *header = reinterpret_cast<const LixThemeHeader *>(buf);
    kprintf("[THEME] Package '%s' successfully imported:\n", filepath);
    kprintf("        Name: '%s' | Author: '%s' | Layout: %s\n",
            header->name, header->author,
            (header->layout == 0) ? "Windows 10 Classic" : "macOS GNOME Dock");
    kprintf("        Accent: 0x%08X | Assets Bundled: %u (Self-Contained Dependency Verified)\n",
            header->accent_color, header->asset_count);

    if (out_theme) {
        strncpy(out_theme->name, header->name, sizeof(out_theme->name) - 1);
        out_theme->accent_color = header->accent_color;
        out_theme->desktop_bg = header->desktop_bg;
        out_theme->window_bg = header->window_bg;
        out_theme->titlebar_active_bg = header->titlebar_active_bg;
        out_theme->titlebar_inactive_bg = header->titlebar_inactive_bg;
        out_theme->titlebar_fg = header->titlebar_fg;
        out_theme->panel_bg = header->panel_bg;
        out_theme->border_color = header->border_color;
        out_theme->layout = static_cast<DesktopLayout>(header->layout);
    }

    kfree(buf);
    return 0;
}

bool theme_apply_package(const char *filepath) {
    ThemeConfig cfg{};
    int res = theme_import_package(filepath, &cfg);
    if (res == 0) {
        wm_set_theme(cfg);
        wm_render();
        kprintf("[THEME] Theme '%s' applied to Window Manager successfully.\n", cfg.name);
        return true;
    }

    // On any error or security violation, ensure safe fallback is active
    wm_set_theme(themes::WINDOWS_10_DARK);
    wm_render();
    kprintf("[THEME] Safe fallback theme 'Windows 10 Dark' restored.\n");
    return false;
}
