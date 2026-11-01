#include "rtc.h"
#include "io.h"

namespace {
    constexpr uint16_t CMOS_ADDR_PORT = 0x70;
    constexpr uint16_t CMOS_DATA_PORT = 0x71;

    uint8_t cmos_read(uint8_t reg) {
        outb(CMOS_ADDR_PORT, (1 << 7) | reg); // Disable NMI while reading
        io_wait();
        return inb(CMOS_DATA_PORT);
    }

    bool is_update_in_progress(void) {
        return (cmos_read(0x0A) & 0x80) != 0;
    }

    uint8_t bcd_to_binary(uint8_t val) {
        return ((val / 16) * 10) + (val & 0x0F);
    }

    size_t rtc_driver_read(void *buffer, size_t count) {
        if (!buffer || count < sizeof(rtc_time)) {
            return 0;
        }

        rtc_time t = rtc_read_time();
        *static_cast<rtc_time *>(buffer) = t;
        return sizeof(rtc_time);
    }

    driver g_rtc_drv_instance = {
        "CMOS Real-Time Clock",
        driver_type::RTC,
        rtc_init,
        rtc_driver_read,
        nullptr
    };
}

bool rtc_init(void) {
    // Read status registers to ensure CMOS is responsive
    uint8_t status_b = cmos_read(0x0B);
    (void)status_b;
    return true;
}

rtc_time rtc_read_time(void) {
    // Wait until RTC is not updating to avoid torn reads
    while (is_update_in_progress()) {
        asm volatile("pause");
    }

    uint8_t sec   = cmos_read(0x00);
    uint8_t min   = cmos_read(0x02);
    uint8_t hour  = cmos_read(0x04);
    uint8_t day   = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year  = cmos_read(0x09);
    uint8_t century = cmos_read(0x32); // May or may not be present

    uint8_t status_b = cmos_read(0x0B);
    bool bcd = !(status_b & 0x04);
    bool hour_12 = !(status_b & 0x02);

    if (bcd) {
        sec   = bcd_to_binary(sec);
        min   = bcd_to_binary(min);
        hour  = bcd_to_binary(hour & 0x7F) | (hour & 0x80);
        day   = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year  = bcd_to_binary(year);
        if (century != 0) {
            century = bcd_to_binary(century);
        }
    }

    // Convert 12-hour clock to 24-hour clock if necessary
    if (hour_12 && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }

    uint16_t full_year;
    if (century != 0) {
        full_year = (static_cast<uint16_t>(century) * 100) + year;
    } else {
        full_year = 2000 + year;
    }

    return rtc_time{ full_year, month, day, hour, min, sec };
}

const driver *rtc_get_driver(void) {
    return &g_rtc_drv_instance;
}
