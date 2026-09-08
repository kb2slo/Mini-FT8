#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} external_rtc_datetime_t;

esp_err_t external_rtc_init(void);
bool external_rtc_available(void);
esp_err_t external_rtc_read_datetime(external_rtc_datetime_t *out_datetime);
esp_err_t external_rtc_write_datetime(const external_rtc_datetime_t *datetime);

#ifdef __cplusplus
}
#endif
