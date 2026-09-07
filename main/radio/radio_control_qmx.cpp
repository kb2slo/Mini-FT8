#include "radio_control_backend.h"
#include "radio_ta_format.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"

#include "stream_uac.h"

static const char* TAG = "RADIO_QMX";

static bool s_power_swr_poll_disabled;
static int s_power_swr_poll_misses;
static constexpr int kPowerSwrPollGiveUp = 4;

static bool qmx_ready(void) {
    return cat_cdc_ready();
}

static esp_err_t qmx_send_cmd(const char* cmd, uint32_t timeout_ms) {
    if (!cat_cdc_ready()) return ESP_ERR_INVALID_STATE;
    return cat_cdc_send(reinterpret_cast<const uint8_t*>(cmd), strlen(cmd), timeout_ms);
}

static esp_err_t qmx_sync_frequency_mode(int freq_hz) {
    const char* md = "MD6;";
    esp_err_t err = qmx_send_cmd(md, 200);
    if (err != ESP_OK) return err;

    err = qmx_send_cmd("FR0;", 200);
    if (err != ESP_OK) return err;

    err = qmx_send_cmd("FT0;", 200);
    if (err != ESP_OK) return err;

    char fa[32];
    snprintf(fa, sizeof(fa), "FA%011d;", freq_hz);
    err = qmx_send_cmd(fa, 200);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "QMX sync ok freq=%d", freq_hz);
    }
    return err;
}

static esp_err_t qmx_begin_tx(int freq_hz, int tx_base_hz) {
    (void)freq_hz;
    (void)tx_base_hz;

    const char* md = "MD6;";
    esp_err_t err = qmx_send_cmd(md, 200);
    if (err != ESP_OK) return err;

    const char* tx = "TX;";
    err = qmx_send_cmd(tx, 200);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "QMX TX start");
    }
    return err;
}

static esp_err_t qmx_set_tone_hz(float tone_hz) {
    char ta[32];
    radio_ta_format(tone_hz, ta, sizeof(ta));
    return qmx_send_cmd(ta, 10);
}

static esp_err_t qmx_end_tx(void) {
    cat_cdc_reset_parsed_meters();
    const char* rx = "RX;";
    esp_err_t err = qmx_send_cmd(rx, 200);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "QMX TX stop");
    }
    return err;
}

static esp_err_t qmx_set_tune(bool enable, int freq_hz, int tone_hz) {
    if (!enable) {
        return qmx_end_tx();
    }

    esp_err_t err = qmx_sync_frequency_mode(freq_hz);
    if (err != ESP_OK) return err;

    const char* tx = "TX;";
    err = qmx_send_cmd(tx, 200);
    if (err != ESP_OK) return err;

    return qmx_set_tone_hz((float)tone_hz);
}

static esp_err_t qmx_on_audio_start(void) {
    ESP_LOGI(TAG, "QMX CAT backend initialized");
    return ESP_OK;
}

static esp_err_t qmx_set_time(int hour, int minute, int second) {
    char tm[32];
    snprintf(tm, sizeof(tm), "TM%02d%02d%02d;", hour, minute, second);
    esp_err_t err = qmx_send_cmd(tm, 200);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "QMX CAT time set: %02d:%02d:%02d", hour, minute, second);
    }
    return err;
}

static void qmx_reset_tx_power_swr(void) {
    cat_cdc_reset_parsed_meters();
    s_power_swr_poll_disabled = false;
    s_power_swr_poll_misses = 0;
}

static void qmx_poll_tx_power_swr(void) {
    if (s_power_swr_poll_disabled) {
        return;
    }
    if (!cat_cdc_ready()) {
        s_power_swr_poll_misses++;
        if (s_power_swr_poll_misses >= kPowerSwrPollGiveUp) {
            s_power_swr_poll_disabled = true;
            ESP_LOGW(TAG, "TX power/SWR: CAT not ready, giving up this TX");
        }
        return;
    }

    const esp_err_t err =
        cat_cdc_send(reinterpret_cast<const uint8_t*>("PC;SW;"), 6, 10);
    if (err != ESP_OK) {
        s_power_swr_poll_disabled = true;
        ESP_LOGW(TAG, "TX power/SWR: CAT send failed (%s), giving up this TX",
                 esp_err_to_name(err));
        return;
    }

    int pc = -1;
    int sw = -1;
    if (cat_cdc_get_parsed_meters(&pc, &sw) && (pc >= 0 || sw >= 0)) {
        s_power_swr_poll_misses = 0;
        return;
    }
    s_power_swr_poll_misses++;
    if (s_power_swr_poll_misses >= kPowerSwrPollGiveUp) {
        s_power_swr_poll_disabled = true;
        ESP_LOGW(TAG, "TX power/SWR: no PC/SW replies (old firmware?), hiding RF line");
    }
}

static bool qmx_get_tx_power_swr(float* power_w, float* swr) {
    int pc = -1;
    int sw = -1;
    if (!cat_cdc_get_parsed_meters(&pc, &sw)) {
        return false;
    }
    if (power_w) {
        *power_w = pc >= 0 ? pc / 10.0f : -1.f;
    }
    if (swr) {
        *swr = sw >= 0 ? sw / 100.0f : -1.f;
    }
    return pc >= 0 || sw >= 0;
}

static const radio_control_capabilities_t k_capabilities = {
    .has_wattmeter = true,
    .has_swr = true,
    .can_set_time = true,
    .audio_is_uac = true,
    .can_md8_tune = true,
    .can_ps0 = true,
};

static const radio_control_ops_t k_ops = {
    .name = "qmx",
    .capabilities = &k_capabilities,
    .ready = qmx_ready,
    .on_audio_start = qmx_on_audio_start,
    .sync_frequency_mode = qmx_sync_frequency_mode,
    .begin_tx = qmx_begin_tx,
    .set_tone_hz = qmx_set_tone_hz,
    .end_tx = qmx_end_tx,
    .set_tune = qmx_set_tune,
    .set_time = qmx_set_time,
    .reset_tx_power_swr = qmx_reset_tx_power_swr,
    .poll_tx_power_swr = qmx_poll_tx_power_swr,
    .get_tx_power_swr = qmx_get_tx_power_swr,
    .begin_cpfsk_tx = nullptr,
};

const radio_control_ops_t* radio_control_qmx_get_ops(void) {
    return &k_ops;
}
