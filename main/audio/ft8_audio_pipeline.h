#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FT8_AUDIO_WATERFALL_ROW_WIDTH 240

typedef int (*ft8_audio_read_cb_t)(void* ctx, float* out, int max_samples);
typedef bool (*ft8_audio_should_stop_cb_t)(void* ctx);
typedef void (*ft8_audio_block_cb_t)(void* ctx);

typedef struct {
    const char* tag;
    void* ctx;
    ft8_audio_read_cb_t read;
    ft8_audio_should_stop_cb_t should_stop;
    ft8_audio_block_cb_t on_block_processed;
} ft8_audio_pipeline_config_t;

void ft8_audio_pipeline_run(const ft8_audio_pipeline_config_t* cfg);
void ft8_audio_pipeline_clear_latest_waterfall_row(void);
bool ft8_audio_pipeline_get_latest_waterfall_row(uint8_t* out_row, int out_len);
