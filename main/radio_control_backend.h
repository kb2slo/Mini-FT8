#pragma once

#include <stddef.h>
#include <stdint.h>

#include "radio_control.h"

typedef struct {
    const char* name;
    const radio_control_capabilities_t* capabilities;
    bool (*ready)(void);
    esp_err_t (*on_audio_start)(void);
    esp_err_t (*sync_frequency_mode)(int freq_hz);
    esp_err_t (*begin_tx)(int freq_hz, int tx_base_hz);
    esp_err_t (*set_tone_hz)(float tone_hz);
    esp_err_t (*end_tx)(void);
    esp_err_t (*set_tune)(bool enable, int freq_hz, int tone_hz);
    esp_err_t (*set_time)(int hour, int minute, int second);
    void (*reset_tx_power_swr)(void);
    void (*poll_tx_power_swr)(void);
    bool (*get_tx_power_swr)(float* power_w, float* swr);
    esp_err_t (*begin_cpfsk_tx)(float base_hz,
                                const uint8_t* symbols,
                                size_t symbol_count,
                                float tone_spacing_hz,
                                uint32_t samples_per_symbol);
} radio_control_ops_t;

const radio_control_ops_t* radio_control_qmx_get_ops(void);
const radio_control_ops_t* radio_control_qdx_get_ops(void);
const radio_control_ops_t* radio_control_kh1_get_ops(void);
void radio_control_kh1_set_enabled(bool enabled);
bool radio_control_kh1_is_enabled(void);
esp_err_t radio_control_kh1_diag_test(char test_key, int freq_hz, int offset_hz, bool* out_fa_sent);
