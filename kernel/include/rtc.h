#ifndef LIXOS_RTC_H
#define LIXOS_RTC_H

#include <stdint.h>
#include <stddef.h>
#include "driver.h"

struct rtc_time {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
};

// Initialize RTC driver
bool rtc_init(void);

// Read current hardware real-time clock
rtc_time rtc_read_time(void);

// Get the driver descriptor
const driver *rtc_get_driver(void);

#endif // LIXOS_RTC_H
