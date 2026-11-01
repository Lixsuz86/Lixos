#ifndef LIXOS_DRIVER_H
#define LIXOS_DRIVER_H

#include <stdint.h>
#include <stddef.h>

enum class driver_type {
    GENERIC,
    KEYBOARD,
    RTC,
    SERIAL,
    POWER,
    STORAGE,
    NETWORK
};

struct driver {
    const char   *name;
    driver_type   type;
    bool        (*init)(void);
    size_t      (*read)(void *buffer, size_t count);
    size_t      (*write)(const void *buffer, size_t count);
};

// Register a hardware driver into the kernel registry
bool driver_register(const driver *drv);

// Initialize all registered drivers
void driver_init_all(void);

// Find a registered driver by type
const driver *driver_find(driver_type type);

#endif // LIXOS_DRIVER_H
