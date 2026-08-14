#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ft8_audio_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UAC_SAMPLE_RATE 48000
#define UAC_BIT_RESOLUTION 24
#define UAC_CHANNELS 2

#define UAC_BUFFER_SIZE 4000
#define UAC_BUFFER_THRESHOLD 600

typedef enum {
    UAC_STATE_IDLE,
    UAC_STATE_WAITING,
    UAC_STATE_CONNECTED,
    UAC_STATE_STREAMING,
    UAC_STATE_ERROR,
} uac_stream_state_t;

typedef enum {
    UAC_PROFILE_QMX = 0,
    UAC_PROFILE_GENERIC_USB = 1,
} uac_stream_profile_t;

#define UAC_WATERFALL_ROW_WIDTH FT8_AUDIO_WATERFALL_ROW_WIDTH

uac_stream_state_t uac_get_state(void);
bool uac_is_streaming(void);
bool uac_start(void);
bool uac_start_with_profile(uac_stream_profile_t profile);
void uac_stop(void);
esp_err_t uac_ensure_host_uninstalled(void);
bool uac_usb_host_released(void);
bool uac_qmx_detected(void);

const char* uac_get_status_string(void);
const char* uac_get_debug_line1(void);
const char* uac_get_debug_line2(void);
bool uac_get_latest_waterfall_row(uint8_t* out_row, int out_len);

bool cat_cdc_ready(void);
esp_err_t cat_cdc_send(const uint8_t* data, size_t len, uint32_t timeout_ms);

bool uac_tx_begin_cpfsk(float base_hz,
                        const uint8_t* symbols,
                        size_t symbol_count,
                        float tone_spacing_hz,
                        uint32_t samples_per_symbol);
bool uac_tx_begin_tune(float tone_hz);
void uac_tx_end(void);

#ifdef __cplusplus
}
#endif
