#ifndef LIXOS_STRING_H
#define LIXOS_STRING_H

#include <stddef.h>
#include <stdint.h>

extern "C" {
    void *memset(void *dest, int ch, size_t count);
    void *memcpy(void *dest, const void *src, size_t count);
    void *memmove(void *dest, const void *src, size_t count);
    int   memcmp(const void *lhs, const void *rhs, size_t count);

    size_t strlen(const char *str);
    char  *strcpy(char *dest, const char *src);
    char  *strncpy(char *dest, const char *src, size_t count);
    int    strcmp(const char *lhs, const char *rhs);
    int    strncmp(const char *lhs, const char *rhs, size_t count);
    char  *strcat(char *dest, const char *src);
}

#endif // LIXOS_STRING_H
