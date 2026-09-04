#include "audio_source.h"

#include "ft8_audio_pipeline.h"
#include "stream_uac.h"

#include "esp_log.h"

static const char* TAG = "AUDIO_SRC";
static audio_source_backend_t s_backend = AUDIO_SOURCE_QMX_UAC;
static audio_source_backend_t s_active_backend = AUDIO_SOURCE_QMX_UAC;
static bool s_have_active_backend = false;

void audio_source_set_backend(audio_source_backend_t backend) {
    s_backend = backend;
}

audio_source_backend_t audio_source_get_backend(void) {
    return s_backend;
}

const char* audio_source_backend_name(audio_source_backend_t backend) {
    switch (backend) {
    case AUDIO_SOURCE_QMX_UAC:
        return "qmx_uac";
    default:
        return "unknown";
    }
}

bool audio_source_start(void) {
    ESP_LOGI(TAG, "Start audio source backend=%s", audio_source_backend_name(s_backend));
    bool ok = uac_start_with_profile(UAC_PROFILE_QMX);

    if (ok) {
        s_active_backend = s_backend;
        s_have_active_backend = true;
    }
    return ok;
}

void audio_source_stop(void) {
    uac_stop();
    s_have_active_backend = false;
}

bool audio_source_is_streaming(void) {
    return uac_is_streaming();
}

bool audio_source_qmx_detected(void) {
    return uac_qmx_detected();
}

const char* audio_source_get_status_string(void) {
    return uac_get_status_string();
}

const char* audio_source_get_debug_line1(void) {
    return uac_get_debug_line1();
}

const char* audio_source_get_debug_line2(void) {
    return uac_get_debug_line2();
}

bool audio_source_get_latest_waterfall_row(uint8_t* out_row, int out_len) {
    return ft8_audio_pipeline_get_latest_waterfall_row(out_row, out_len);
}
