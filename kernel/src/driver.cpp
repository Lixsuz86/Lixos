#include "driver.h"
#include "kprint.h"

namespace {
    constexpr size_t MAX_DRIVERS = 32;
    const driver *g_drivers[MAX_DRIVERS];
    size_t g_driver_count = 0;
}

bool driver_register(const driver *drv) {
    if (!drv || g_driver_count >= MAX_DRIVERS) {
        return false;
    }

    g_drivers[g_driver_count++] = drv;
    return true;
}

void driver_init_all(void) {
    kprintf("[DRV ] Initializing Registered Hardware Drivers (%u Total):\n",
            static_cast<unsigned int>(g_driver_count));

    for (size_t i = 0; i < g_driver_count; ++i) {
        const driver *drv = g_drivers[i];
        if (drv && drv->init) {
            bool ok = drv->init();
            kprintf("       [%u] Driver '%s': %s\n",
                    static_cast<unsigned int>(i),
                    drv->name,
                    ok ? "OK" : "FAILED");
        }
    }
}

const driver *driver_find(driver_type type) {
    for (size_t i = 0; i < g_driver_count; ++i) {
        if (g_drivers[i] && g_drivers[i]->type == type) {
            return g_drivers[i];
        }
    }
    return nullptr;
}
