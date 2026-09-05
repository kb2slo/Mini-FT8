#include "external_rtc.h"

#include "board_i2c.h"
#include "board_pins.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#include <stddef.h>

namespace {

constexpr uint8_t kDs3231Addr = 0x68;
constexpr uint32_t kDs3231SpeedHz = 100000;
constexpr int kDs3231TimeoutMs = 100;
constexpr uint8_t kDs3231SecondsReg = 0x00;
constexpr uint8_t kDs3231StatusReg = 0x0F;
constexpr uint8_t kDs3231StatusOsf = 0x80;
constexpr int64_t kSecondsPerDay = 86400;

const char *kTag = "external_rtc";

i2c_master_dev_handle_t s_ds3231 = nullptr;
bool s_initialized = false;
bool s_available = false;

bool is_leap_year(uint16_t year) {
    return (year % 4U == 0U) && ((year % 100U) != 0U || (year % 400U) == 0U);
}

uint8_t days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t days[] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
    };

    if (month < 1U || month > 12U) {
        return 0U;
    }
    if (month == 2U && is_leap_year(year)) {
        return 29U;
    }
    return days[month - 1U];
}

bool datetime_valid(const external_rtc_datetime_t *datetime) {
    if (!datetime) {
        return false;
    }
    if (datetime->year < 2024U || datetime->year > 2099U) {
        return false;
    }
    if (datetime->month < 1U || datetime->month > 12U) {
        return false;
    }
    if (datetime->day < 1U || datetime->day > days_in_month(datetime->year, datetime->month)) {
        return false;
    }
    return datetime->hour <= 23U && datetime->minute <= 59U && datetime->second <= 59U;
}

int64_t datetime_to_epoch(const external_rtc_datetime_t *datetime) {
    int64_t days = 0;
    for (uint16_t year = 1970U; year < datetime->year; ++year) {
        days += is_leap_year(year) ? 366 : 365;
    }
    for (uint8_t month = 1U; month < datetime->month; ++month) {
        days += days_in_month(datetime->year, month);
    }
    days += (int64_t)datetime->day - 1;
    return days * kSecondsPerDay + (int64_t)datetime->hour * 3600 +
           (int64_t)datetime->minute * 60 + datetime->second;
}

uint8_t day_of_week(const external_rtc_datetime_t *datetime) {
    const int64_t days = datetime_to_epoch(datetime) / kSecondsPerDay;
    return (uint8_t)(((days + 3) % 7) + 1);
}

bool bcd_to_u8(uint8_t bcd, uint8_t *out_value) {
    const uint8_t high = (uint8_t)((bcd >> 4) & 0x0F);
    const uint8_t low = (uint8_t)(bcd & 0x0F);

    if (!out_value || high > 9U || low > 9U) {
        return false;
    }

    *out_value = (uint8_t)(high * 10U + low);
    return true;
}

uint8_t u8_to_bcd(uint8_t value) {
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

esp_err_t read_reg(uint8_t reg, uint8_t *data, size_t data_len) {
    if (!s_ds3231 || !data || data_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(s_ds3231,
                                       &reg,
                                       sizeof(reg),
                                       data,
                                       data_len,
                                       kDs3231TimeoutMs);
}

esp_err_t write_reg(uint8_t reg, uint8_t value) {
    if (!s_ds3231) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[] = {reg, value};
    return i2c_master_transmit(s_ds3231, data, sizeof(data), kDs3231TimeoutMs);
}

}  // namespace

esp_err_t external_rtc_init(void) {
    if (s_initialized) {
        return s_available ? ESP_OK : ESP_ERR_NOT_FOUND;
    }
    s_initialized = true;

    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t err = board_i2c_get_bus(&bus);
    if (err != ESP_OK) {
        ESP_LOGW(kTag,
                 "DS3231 not detected at 0x%02x on I2C SDA=%d SCL=%d: %s",
                 kDs3231Addr,
                 BOARD_I2C_SDA,
                 BOARD_I2C_SCL,
                 esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = kDs3231Addr;
    dev_cfg.scl_speed_hz = kDs3231SpeedHz;

    err = i2c_master_bus_add_device(bus, &dev_cfg, &s_ds3231);
    if (err != ESP_OK) {
        ESP_LOGW(kTag,
                 "DS3231 not detected at 0x%02x on I2C SDA=%d SCL=%d: %s",
                 kDs3231Addr,
                 BOARD_I2C_SDA,
                 BOARD_I2C_SCL,
                 esp_err_to_name(err));
        s_ds3231 = nullptr;
        return err;
    }

    uint8_t reg = kDs3231SecondsReg;
    uint8_t seconds_reg = 0;
    err = i2c_master_transmit_receive(s_ds3231,
                                      &reg,
                                      sizeof(reg),
                                      &seconds_reg,
                                      sizeof(seconds_reg),
                                      kDs3231TimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGW(kTag,
                 "DS3231 not detected at 0x%02x on I2C SDA=%d SCL=%d: %s",
                 kDs3231Addr,
                 BOARD_I2C_SDA,
                 BOARD_I2C_SCL,
                 esp_err_to_name(err));
        (void)i2c_master_bus_rm_device(s_ds3231);
        s_ds3231 = nullptr;
        return err;
    }

    s_available = true;
    ESP_LOGI(kTag,
             "DS3231 detected at 0x%02x on I2C SDA=%d SCL=%d seconds_reg=0x%02x",
             kDs3231Addr,
             BOARD_I2C_SDA,
             BOARD_I2C_SCL,
             seconds_reg);
    return ESP_OK;
}

bool external_rtc_available(void) {
    return s_available;
}

esp_err_t external_rtc_read_datetime(external_rtc_datetime_t *out_datetime) {
    if (!out_datetime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        esp_err_t err = external_rtc_init();
        if (err != ESP_OK) {
            return err;
        }
    }
    if (!s_available || !s_ds3231) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t regs[7] = {};
    uint8_t status = 0;
    esp_err_t err = read_reg(kDs3231SecondsReg, regs, sizeof(regs));
    if (err != ESP_OK) {
        return err;
    }
    err = read_reg(kDs3231StatusReg, &status, sizeof(status));
    if (err != ESP_OK) {
        return err;
    }

    if ((regs[2] & 0x40U) != 0U) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if ((regs[5] & 0x80U) != 0U) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t seconds = 0;
    uint8_t minutes = 0;
    uint8_t hours = 0;
    uint8_t day = 0;
    uint8_t month = 0;
    uint8_t year = 0;
    if (!bcd_to_u8((uint8_t)(regs[0] & 0x7FU), &seconds) ||
        !bcd_to_u8((uint8_t)(regs[1] & 0x7FU), &minutes) ||
        !bcd_to_u8((uint8_t)(regs[2] & 0x3FU), &hours) ||
        !bcd_to_u8((uint8_t)(regs[4] & 0x3FU), &day) ||
        !bcd_to_u8((uint8_t)(regs[5] & 0x1FU), &month) ||
        !bcd_to_u8(regs[6], &year)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    external_rtc_datetime_t datetime = {};
    datetime.year = (uint16_t)(2000U + year);
    datetime.month = month;
    datetime.day = day;
    datetime.hour = hours;
    datetime.minute = minutes;
    datetime.second = seconds;

    if (!datetime_valid(&datetime)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if ((status & kDs3231StatusOsf) != 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    *out_datetime = datetime;
    return ESP_OK;
}

esp_err_t external_rtc_write_datetime(const external_rtc_datetime_t *datetime) {
    if (!datetime_valid(datetime)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        esp_err_t err = external_rtc_init();
        if (err != ESP_OK) {
            return err;
        }
    }
    if (!s_available || !s_ds3231) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[8] = {};
    data[0] = kDs3231SecondsReg;
    data[1] = u8_to_bcd(datetime->second);
    data[2] = u8_to_bcd(datetime->minute);
    data[3] = u8_to_bcd(datetime->hour);
    data[4] = u8_to_bcd(day_of_week(datetime));
    data[5] = u8_to_bcd(datetime->day);
    data[6] = u8_to_bcd(datetime->month);
    data[7] = u8_to_bcd((uint8_t)(datetime->year - 2000U));

    esp_err_t err = i2c_master_transmit(s_ds3231, data, sizeof(data), kDs3231TimeoutMs);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t status = 0;
    err = read_reg(kDs3231StatusReg, &status, sizeof(status));
    if (err != ESP_OK) {
        return err;
    }

    status = (uint8_t)(status & ~kDs3231StatusOsf);
    return write_reg(kDs3231StatusReg, status);
}
