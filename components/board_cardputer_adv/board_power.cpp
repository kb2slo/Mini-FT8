#include "board_power.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char* TAG = "BOARD_POWER";

// M5Unified uses ADC1 GPIO10 for M5CardputerADV battery measurement.
// Battery divider ratio is 2.0.
static constexpr adc_unit_t kAdcUnit = ADC_UNIT_1;
static constexpr adc_channel_t kBatAdcChannel = ADC_CHANNEL_9;  // GPIO10 on ESP32-S3 ADC1
static constexpr float kAdcRatio = 2.0f;

static adc_oneshot_unit_handle_t g_adc = nullptr;
static adc_cali_handle_t g_cali = nullptr;
static bool g_initialized = false;
static bool g_cali_ok = false;
static bool g_tx_halted = false;
static bool g_writes_blocked = false;
static int g_ema_mv = -1;
static int64_t g_tx_low_since_us = 0;
static int64_t g_write_low_since_us = 0;

// TX halt is conservative for a live pack under radio load. Flash writes use a
// lower, slower gate so a healthy (or only moderately discharged) cell cannot
// silently drop ADIF / Station.txt while QSOs still complete.
static constexpr int kTxEnterMv = 3400;
static constexpr int kTxExitMv = 3550;
static constexpr int kWriteEnterMv = 3250;
static constexpr int kWriteExitMv = 3400;
// Yellow UI only: just above TX halt so mid-pack TX sag is not an alarm.
static constexpr int kWarnMv = 3450;
static constexpr int64_t kHoldUs = 8 * 1000 * 1000;
// Below this, the pack is disconnected or the ADC is junk (USB, switch OFF).
static constexpr int kSenseMinMv = 2800;
static constexpr int kAdcAvgSamples = 8;
static constexpr int kEmaNum = 3;
static constexpr int kEmaDen = 4;

static bool hold_hysteresis(bool current, int ema, int enter_mv, int exit_mv, int64_t* low_since_us)
{
    const int64_t now = esp_timer_get_time();
    if (ema <= enter_mv) {
        if (*low_since_us == 0) {
            *low_since_us = now;
        }
        if ((now - *low_since_us) >= kHoldUs) {
            return true;
        }
        return current;
    }
    *low_since_us = 0;
    if (ema >= exit_mv) {
        return false;
    }
    return current;
}

static int voltage_to_percent(int mv)
{
    // Same linear map as M5Unified / M5Launcher Cardputer (GPIO10 ×2):
    //   percent = (mv - 3300) * 100 / (4150 - 3350), clamped 0..100
    int level = (mv - 3300) * 100 / (4150 - 3350);
    if (level < 0) return 0;
    if (level >= 100) return 100;
    return level;
}

esp_err_t board_power_init(void)
{
    if (g_initialized) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t init_cfg = {};
    init_cfg.unit_id = kAdcUnit;

    ESP_RETURN_ON_ERROR(
        adc_oneshot_new_unit(&init_cfg, &g_adc),
        TAG,
        "adc_oneshot_new_unit failed"
    );

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_12;

    ESP_RETURN_ON_ERROR(
        adc_oneshot_config_channel(g_adc, kBatAdcChannel, &chan_cfg),
        TAG,
        "adc_oneshot_config_channel failed"
    );

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = kAdcUnit;
    cali_cfg.chan = kBatAdcChannel;
    cali_cfg.atten = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_12;

    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &g_cali) == ESP_OK) {
        g_cali_ok = true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = kAdcUnit;
    cali_cfg.atten = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_12;

    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &g_cali) == ESP_OK) {
        g_cali_ok = true;
    }
#endif

    ESP_LOGI(TAG,
             "battery ADC init OK: unit=1 channel=9 GPIO10 ratio=%.1f cali=%d",
             (double)kAdcRatio,
             (int)g_cali_ok);

    g_initialized = true;
    return ESP_OK;
}

esp_err_t board_power_read(board_power_status_t* out_status)
{
    if (!out_status) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_status, 0, sizeof(*out_status));
    out_status->voltage_mv = -1;
    out_status->percent = -1;
    out_status->charging_known = false;
    out_status->charging = false;

    ESP_RETURN_ON_ERROR(board_power_init(), TAG, "board_power_init failed");

    int raw_sum = 0;
    for (int i = 0; i < kAdcAvgSamples; ++i) {
        int raw = 0;
        ESP_RETURN_ON_ERROR(
            adc_oneshot_read(g_adc, kBatAdcChannel, &raw),
            TAG,
            "adc_oneshot_read failed"
        );
        raw_sum += raw;
    }
    int raw = raw_sum / kAdcAvgSamples;

    int adc_mv = raw;
    if (g_cali_ok && g_cali) {
        ESP_RETURN_ON_ERROR(
            adc_cali_raw_to_voltage(g_cali, raw, &adc_mv),
            TAG,
            "adc_cali_raw_to_voltage failed"
        );
    }

    int bat_mv = (int)(adc_mv * kAdcRatio + 0.5f);
    const bool sense_ok = bat_mv >= kSenseMinMv;
    if (sense_ok) {
        if (g_ema_mv < 0) {
            g_ema_mv = bat_mv;
        } else {
            g_ema_mv = (g_ema_mv * kEmaNum + bat_mv) / kEmaDen;
        }

        const bool was_tx = g_tx_halted;
        const bool was_wr = g_writes_blocked;
        g_tx_halted = hold_hysteresis(g_tx_halted, g_ema_mv, kTxEnterMv, kTxExitMv, &g_tx_low_since_us);
        g_writes_blocked = hold_hysteresis(g_writes_blocked, g_ema_mv, kWriteEnterMv, kWriteExitMv, &g_write_low_since_us);
        if (g_tx_halted && !was_tx) {
            ESP_LOGW(TAG, "battery TX halt enter ema=%d mV", g_ema_mv);
        } else if (!g_tx_halted && was_tx) {
            ESP_LOGI(TAG, "battery TX halt exit ema=%d mV", g_ema_mv);
        }
        if (g_writes_blocked && !was_wr) {
            ESP_LOGW(TAG, "battery write halt enter ema=%d mV", g_ema_mv);
        } else if (!g_writes_blocked && was_wr) {
            ESP_LOGI(TAG, "battery write halt exit ema=%d mV", g_ema_mv);
        }
    } else {
        ESP_LOGD(TAG, "battery sense ignored mv=%d", bat_mv);
        g_tx_low_since_us = 0;
        g_write_low_since_us = 0;
        if (g_ema_mv < 0) {
            g_tx_halted = false;
            g_writes_blocked = false;
        }
    }

    out_status->valid = true;
    out_status->voltage_mv = sense_ok ? g_ema_mv : bat_mv;
    out_status->percent = sense_ok ? voltage_to_percent(g_ema_mv) : -1;
    out_status->pack_present = sense_ok;
    out_status->halted = g_tx_halted;
    out_status->writes_blocked = g_writes_blocked;
    out_status->warn = g_tx_halted || g_writes_blocked || (sense_ok && g_ema_mv <= kWarnMv);

    return ESP_OK;
}

void board_power_format_charge_stripe(const board_power_status_t* status, char* buf, size_t buf_len)
{
    if (!buf || buf_len == 0) {
        return;
    }
    if (!status || !status->valid || !status->pack_present || status->percent < 0) {
        snprintf(buf, buf_len, "SW ON to charge");
        return;
    }
    snprintf(buf, buf_len, "%d %%", status->percent);
}

bool board_power_halted(void)
{
    return g_tx_halted;
}

bool board_power_writes_blocked(void)
{
    return g_writes_blocked;
}
