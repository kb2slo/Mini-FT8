#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RADIO_CONTROL_QMX = 0,
    RADIO_CONTROL_KH1_CAT = 1,
    RADIO_CONTROL_QDX = 2,
} radio_control_backend_t;

typedef struct {
    bool has_wattmeter;
    bool has_swr;
    bool can_set_time;
    bool audio_is_uac;
    bool can_md8_tune;
    bool can_ps0;
} radio_control_capabilities_t;

void radio_control_set_backend(radio_control_backend_t backend);
radio_control_backend_t radio_control_get_backend(void);
const char* radio_control_backend_name(radio_control_backend_t backend);
const radio_control_capabilities_t* radio_control_get_capabilities(void);

bool radio_control_ready(void);

esp_err_t radio_control_on_audio_start(void);
esp_err_t radio_control_sync_frequency_mode(int freq_hz);
esp_err_t radio_control_begin_tx(int freq_hz, int tx_base_hz);
esp_err_t radio_control_set_tone_hz(float tone_hz);
esp_err_t radio_control_end_tx(void);
esp_err_t radio_control_set_tune(bool enable, int freq_hz, int tone_hz);
esp_err_t radio_control_set_time(int hour, int minute, int second);
esp_err_t radio_control_begin_cpfsk_tx(float base_hz,
                                       const uint8_t* symbols,
                                       size_t symbol_count,
                                       float tone_spacing_hz,
                                       uint32_t samples_per_symbol);

void radio_control_reset_tx_power_swr(void);
void radio_control_poll_tx_power_swr(void);
bool radio_control_get_tx_power_swr(float* power_w, float* swr);

#ifdef __cplusplus
}
#endif
