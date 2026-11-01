#include "kprint.h"
#include "serial.h"
#include "fb.h"

namespace {
    void console_putc(char c) {
        serial_putc(c);
        if (fb_is_available()) {
            fb_console_putc(c);
        }
    }

    void console_puts(const char *str) {
        serial_puts(str);
        if (fb_is_available()) {
            fb_console_puts(str);
        }
    }

    void print_unsigned(uint64_t num, int base, bool uppercase, size_t width = 0, bool pad_zero = false) {
        char buf[65];
        const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
        size_t idx = 0;

        if (num == 0) {
            buf[idx++] = '0';
        } else {
            while (num > 0) {
                buf[idx++] = digits[num % base];
                num /= base;
            }
        }

        while (idx < width) {
            buf[idx++] = pad_zero ? '0' : ' ';
        }

        // Reverse output
        while (idx > 0) {
            console_putc(buf[--idx]);
        }
    }

    void print_signed(int64_t num, size_t width = 0, bool pad_zero = false) {
        if (num < 0) {
            console_putc('-');
            print_unsigned(static_cast<uint64_t>(-num), 10, false, (width > 1) ? (width - 1) : 0, pad_zero);
        } else {
            print_unsigned(static_cast<uint64_t>(num), 10, false, width, pad_zero);
        }
    }
}

void vkprintf(const char *fmt, va_list args) {
    if (!fmt) {
        return;
    }

    for (size_t i = 0; fmt[i] != '\0'; ++i) {
        if (fmt[i] != '%') {
            console_putc(fmt[i]);
            continue;
        }

        ++i; // Skip '%'
        if (fmt[i] == '\0') {
            break;
        }

        bool pad_zero = false;
        size_t min_width = 0;

        if (fmt[i] == '0') {
            pad_zero = true;
            ++i;
        }

        while (fmt[i] >= '0' && fmt[i] <= '9') {
            min_width = (min_width * 10) + (fmt[i] - '0');
            ++i;
        }

        switch (fmt[i]) {
            case 'c': {
                char c = static_cast<char>(va_arg(args, int));
                console_putc(c);
                break;
            }
            case 's': {
                const char *str = va_arg(args, const char *);
                if (!str) {
                    console_puts("(null)");
                } else {
                    console_puts(str);
                }
                break;
            }
            case 'd':
            case 'i': {
                int64_t val = va_arg(args, int);
                print_signed(val, min_width, pad_zero);
                break;
            }
            case 'u': {
                uint64_t val = va_arg(args, unsigned int);
                print_unsigned(val, 10, false, min_width, pad_zero);
                break;
            }
            case 'x': {
                uint64_t val = va_arg(args, unsigned int);
                print_unsigned(val, 16, false, min_width, pad_zero);
                break;
            }
            case 'X': {
                uint64_t val = va_arg(args, unsigned int);
                print_unsigned(val, 16, true, min_width, pad_zero);
                break;
            }
            case 'p': {
                uint64_t val = reinterpret_cast<uint64_t>(va_arg(args, void *));
                console_puts("0x");
                print_unsigned(val, 16, false, min_width, pad_zero);
                break;
            }
            case '%': {
                console_putc('%');
                break;
            }
            default: {
                console_putc('%');
                console_putc(fmt[i]);
                break;
            }
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vkprintf(fmt, args);
    va_end(args);
}
