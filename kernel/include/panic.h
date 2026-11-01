#ifndef LIXOS_PANIC_H
#define LIXOS_PANIC_H

#include "idt.h"

// Trigger a kernel panic with message and optional register dump
[[noreturn]] void panic(const char *msg, const interrupt_frame *frame = nullptr);

#endif // LIXOS_PANIC_H
