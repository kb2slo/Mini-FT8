#include "radio_control.h"

#include "radio_control_backend.h"

#include "esp_log.h"
#include "stream_uac.h"

static const char* TAG = "RADIO_CTRL";

static radio_control_backend_t s_backend = RADIO_CONTROL_QMX;
static bool s_meter_poll_disabled;
static int s_meter_poll_misses;
static constexpr int kMeterPollGiveUp = 4;

static const radio_control_ops_t* current_ops(void) {
    switch (s_backend) {
    case RADIO_CONTROL_KH1_CAT:
        return radio_control_kh1_get_ops();
    case RADIO_CONTROL_QDX:
        return radio_control_qdx_get_ops();
    case RADIO_CONTROL_QMX:
    default:
        return radio_control_qmx_get_ops();
    }
}

void radio_control_set_backend(radio_control_backend_t backend) {
    s_backend = backend;
    const radio_control_ops_t* ops = current_ops();
    ESP_LOGI(TAG, "Selected radio control backend=%s", ops ? ops->name : "unknown");
}

radio_control_backend_t radio_control_get_backend(void) {
    return s_backend;
}

const char* radio_control_backend_name(radio_control_backend_t backend) {
    switch (backend) {
    case RADIO_CONTROL_QMX:
        return "qmx";
    case RADIO_CONTROL_KH1_CAT:
        return "kh1_cat";
    case RADIO_CONTROL_QDX:
        return "qdx";
    default:
        return "unknown";
    }
}

bool radio_control_ready(void) {
    const radio_control_ops_t* ops = current_ops();
    if (!ops || !ops->ready) return false;
    return ops->ready();
}

esp_err_t radio_control_on_audio_start(void) {
    const radio_control_ops_t* ops = current_ops();
    if (!ops || !ops->on_audio_start) return ESP_ERR_INVALID_STATE;
    return ops->on_audio_start();
}

esp_err_t radio_control_sync_frequency_mode(int freq_hz) {
    const radio_control_ops_t* ops = current_ops();
    if (!ops || !ops->sync_frequency_mode) return ESP_ERR_INVALID_STATE;
    return ops->sync_frequency_mode(freq_hz);
}

esp_err_t radio_control_begin_tx(int freq_hz, int tx_base_hz) {
    radio_control_reset_tx_meters();
    const radio_control_ops_t* ops = current_ops();
    if (!ops || !ops->begin_tx) return ESP_ERR_INVALID_STATE;
    return ops->begin_tx(freq_hz, tx_base_hz);
}

esp_err_t radio_control_set_tone_hz(float tone_hz) {
    const radio_control_ops_t* ops = current_ops();
    if (!ops || !ops->set_tone_hz) return ESP_ERR_INVALID_STATE;
    return ops->set_tone_hz(tone_hz);
}

esp_err_t radio_control_end_tx(void) {
    const radio_control_ops_t* ops = current_ops();
    if (!ops || !ops->end_tx) return ESP_ERR_INVALID_STATE;
    return ops->end_tx();
}

esp_err_t radio_control_set_tune(bool enable, int freq_hz, int tone_hz) {
    const radio_control_ops_t* ops = current_ops();
    if (!ops || !ops->set_tune) return ESP_ERR_INVALID_STATE;
    return ops->set_tune(enable, freq_hz, tone_hz);
}

esp_err_t radio_control_set_time(int hour, int minute, int second) {
    const radio_control_ops_t* ops = current_ops();
    if (!ops || !ops->set_time) return ESP_ERR_NOT_SUPPORTED;
    return ops->set_time(hour, minute, second);
}

void radio_control_reset_tx_meters(void) {
    cat_cdc_reset_parsed_meters();
    s_meter_poll_disabled = false;
    s_meter_poll_misses = 0;
}

void radio_control_poll_tx_meters(void) {
    if (s_meter_poll_disabled) {
        return;
    }
    // Only QMX documents PC; (forward watts) and SW; (SWR). QDX PC; is a
    // Kenwood power *setting*, not a wattmeter — never send it. KH1 has no
    // equivalent. QMX PC/SW HUD path has been tested; QDX/KH1 have not.
    if (s_backend != RADIO_CONTROL_QMX) {
        s_meter_poll_disabled = true;
        return;
    }
    if (!cat_cdc_ready()) {
        s_meter_poll_misses++;
        if (s_meter_poll_misses >= kMeterPollGiveUp) {
            s_meter_poll_disabled = true;
            ESP_LOGW(TAG, "TX meters: CAT not ready, giving up this TX");
        }
        return;
    }

    const esp_err_t err =
        cat_cdc_send(reinterpret_cast<const uint8_t*>("PC;SW;"), 6, 10);
    if (err != ESP_OK) {
        s_meter_poll_disabled = true;
        ESP_LOGW(TAG, "TX meters: CAT send failed (%s), giving up this TX",
                 esp_err_to_name(err));
        return;
    }

    int pc = -1;
    int sw = -1;
    if (cat_cdc_get_parsed_meters(&pc, &sw) && (pc >= 0 || sw >= 0)) {
        s_meter_poll_misses = 0;
        return;
    }
    s_meter_poll_misses++;
    if (s_meter_poll_misses >= kMeterPollGiveUp) {
        s_meter_poll_disabled = true;
        ESP_LOGW(TAG, "TX meters: no PC/SW replies (old firmware?), hiding RF line");
    }
}

bool radio_control_get_tx_meters(float* power_w, float* swr) {
    if (s_backend != RADIO_CONTROL_QMX) {
        return false;
    }
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
