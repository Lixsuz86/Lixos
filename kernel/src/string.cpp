#include "string.h"

extern "C" {
    void *memset(void *dest, int ch, size_t count) {
        uint8_t val = static_cast<uint8_t>(ch);
        uint64_t val64 = val;
        val64 |= (val64 << 8);
        val64 |= (val64 << 16);
        val64 |= (val64 << 32);

        uint64_t *d64 = reinterpret_cast<uint64_t *>(dest);
        size_t qwords = count / 8;
        for (size_t i = 0; i < qwords; ++i) {
            d64[i] = val64;
        }

        uint8_t *d8 = reinterpret_cast<uint8_t *>(dest) + (qwords * 8);
        size_t rem = count % 8;
        for (size_t i = 0; i < rem; ++i) {
            d8[i] = val;
        }
        return dest;
    }

    void *memcpy(void *dest, const void *src, size_t count) {
        uint64_t *d64 = reinterpret_cast<uint64_t *>(dest);
        const uint64_t *s64 = reinterpret_cast<const uint64_t *>(src);
        size_t qwords = count / 8;
        for (size_t i = 0; i < qwords; ++i) {
            d64[i] = s64[i];
        }

        uint8_t *d8 = reinterpret_cast<uint8_t *>(dest) + (qwords * 8);
        const uint8_t *s8 = reinterpret_cast<const uint8_t *>(src) + (qwords * 8);
        size_t rem = count % 8;
        for (size_t i = 0; i < rem; ++i) {
            d8[i] = s8[i];
        }
        return dest;
    }

    void *memmove(void *dest, const void *src, size_t count) {
        uint8_t *d = static_cast<uint8_t *>(dest);
        const uint8_t *s = static_cast<const uint8_t *>(src);
        if (d < s) {
            uint64_t *d64 = reinterpret_cast<uint64_t *>(dest);
            const uint64_t *s64 = reinterpret_cast<const uint64_t *>(src);
            size_t qwords = count / 8;
            for (size_t i = 0; i < qwords; ++i) {
                d64[i] = s64[i];
            }
            for (size_t i = qwords * 8; i < count; ++i) {
                d[i] = s[i];
            }
        } else if (d > s) {
            for (size_t i = count; i > 0; --i) {
                d[i - 1] = s[i - 1];
            }
        }
        return dest;
    }

    int memcmp(const void *lhs, const void *rhs, size_t count) {
        const uint8_t *a = static_cast<const uint8_t *>(lhs);
        const uint8_t *b = static_cast<const uint8_t *>(rhs);
        for (size_t i = 0; i < count; ++i) {
            if (a[i] != b[i]) {
                return a[i] - b[i];
            }
        }
        return 0;
    }

    size_t strlen(const char *str) {
        size_t len = 0;
        while (str && str[len] != '\0') ++len;
        return len;
    }

    char *strcpy(char *dest, const char *src) {
        size_t i = 0;
        while (src && src[i] != '\0') {
            dest[i] = src[i];
            ++i;
        }
        dest[i] = '\0';
        return dest;
    }

    char *strncpy(char *dest, const char *src, size_t count) {
        size_t i = 0;
        while (i < count && src && src[i] != '\0') {
            dest[i] = src[i];
            ++i;
        }
        while (i < count) {
            dest[i] = '\0';
            ++i;
        }
        return dest;
    }

    int strcmp(const char *lhs, const char *rhs) {
        while (*lhs && (*lhs == *rhs)) {
            ++lhs;
            ++rhs;
        }
        return *(const unsigned char *)lhs - *(const unsigned char *)rhs;
    }

    int strncmp(const char *lhs, const char *rhs, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            if (lhs[i] != rhs[i] || lhs[i] == '\0') {
                return (unsigned char)lhs[i] - (unsigned char)rhs[i];
            }
        }
        return 0;
    }

    char *strcat(char *dest, const char *src) {
        char *ptr = dest + strlen(dest);
        while (*src != '\0') {
            *ptr++ = *src++;
        }
        *ptr = '\0';
        return dest;
    }
}
