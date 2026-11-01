#ifndef LIXOS_KPRINT_H
#define LIXOS_KPRINT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

// Formatted print to COM1 serial console
void kprintf(const char *fmt, ...);

// Variadic formatted print to COM1 serial console
void vkprintf(const char *fmt, va_list args);

#endif // LIXOS_KPRINT_H
