#include "ft8_audio_pipeline.h"
#include "protocol.h"

#include <cmath>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "core_api_internal.h"
#include "ui.h"

extern "C" {
#include "common/monitor.h"
#include "ft8/constants.h"
#include "ft8/decode.h"
}

extern void log_heap(const char* tag);
extern bool g_decode_enabled;
extern int g_time_osr;
extern int g_freq_osr;
extern int64_t g_decode_slot_idx;
extern volatile bool g_decode_in_progress;
extern volatile int64_t g_decode_applied_slot_idx;
extern volatile bool g_tx_active;
extern volatile bool g_was_txing;
void decode_monitor_results(monitor_t* mon, const monitor_config_t* cfg, bool update_ui);
int64_t rtc_now_ms();

#ifndef FT8_SAMPLE_RATE
#define FT8_SAMPLE_RATE 6000
#endif

static uint8_t s_latest_waterfall_row[FT8_AUDIO_WATERFALL_ROW_WIDTH] = {0};
static bool s_latest_waterfall_row_valid = false;
static portMUX_TYPE s_latest_waterfall_row_lock = portMUX_INITIALIZER_UNLOCKED;

static void push_waterfall_latest(const monitor_t& mon)
{
    if (mon.wf.num_blocks <= 0 || mon.wf.mag == nullptr) return;
    const int block = mon.wf.num_blocks - 1;
    const int num_bins = mon.wf.num_bins;
    const int freq_osr = mon.wf.freq_osr;
    const uint8_t* base = mon.wf.mag + block * mon.wf.block_stride;

    static uint8_t collapsed[480];
    memset(collapsed, 0, num_bins);
    for (int b = 0; b < num_bins; ++b) {
        uint8_t v = 0;
        for (int fs = 0; fs < freq_osr; ++fs) {
            uint8_t val = base[fs * num_bins + b];
            if (val > v) v = val;
        }
        collapsed[b] = v;
    }

    static uint8_t scaled[FT8_AUDIO_WATERFALL_ROW_WIDTH];
    for (int x = 0; x < FT8_AUDIO_WATERFALL_ROW_WIDTH; ++x) {
        int start = (int)((int64_t)x * num_bins / FT8_AUDIO_WATERFALL_ROW_WIDTH);
        int end = (int)((int64_t)(x + 1) * num_bins / FT8_AUDIO_WATERFALL_ROW_WIDTH);
        if (end <= start) end = start + 1;
        uint8_t maxv = 0;
        for (int s = start; s < end && s < num_bins; ++s) {
            if (collapsed[s] > maxv) maxv = collapsed[s];
        }
        scaled[x] = maxv;
    }

    ui_push_waterfall_row(scaled, FT8_AUDIO_WATERFALL_ROW_WIDTH);
    taskENTER_CRITICAL(&s_latest_waterfall_row_lock);
    memcpy(s_latest_waterfall_row, scaled, FT8_AUDIO_WATERFALL_ROW_WIDTH);
    s_latest_waterfall_row_valid = true;
    taskEXIT_CRITICAL(&s_latest_waterfall_row_lock);

    core_fire_waterfall_row(block, collapsed, num_bins,
                            /*swr=*/1.5f, /*pwr=*/2.0f, /*ptt=*/false);
}

void ft8_audio_pipeline_clear_latest_waterfall_row(void)
{
    taskENTER_CRITICAL(&s_latest_waterfall_row_lock);
    memset(s_latest_waterfall_row, 0, sizeof(s_latest_waterfall_row));
    s_latest_waterfall_row_valid = false;
    taskEXIT_CRITICAL(&s_latest_waterfall_row_lock);
}

bool ft8_audio_pipeline_get_latest_waterfall_row(uint8_t* out_row, int out_len)
{
    if (!out_row || out_len < FT8_AUDIO_WATERFALL_ROW_WIDTH) return false;
    bool valid = false;
    taskENTER_CRITICAL(&s_latest_waterfall_row_lock);
    valid = s_latest_waterfall_row_valid;
    if (valid) {
        memcpy(out_row, s_latest_waterfall_row, FT8_AUDIO_WATERFALL_ROW_WIDTH);
    }
    taskEXIT_CRITICAL(&s_latest_waterfall_row_lock);
    return valid;
}

void ft8_audio_pipeline_run(const ft8_audio_pipeline_config_t* cfg)
{
    if (!cfg || !cfg->read || !cfg->should_stop) return;
    const char* tag = cfg->tag ? cfg->tag : "FT8_AUDIO";

    monitor_config_t mon_cfg = {
        .f_min = 200.0f,
        .f_max = 2900.0f,
        .sample_rate = FT8_SAMPLE_RATE,
        .time_osr = g_time_osr,
        .freq_osr = g_freq_osr,
        .protocol = g_protocol->protocol_id
    };

    log_heap("AUDIO_PIPE_BEFORE_MONITOR_INIT");
    monitor_t mon;
    monitor_init(&mon, &mon_cfg);
    log_heap("AUDIO_PIPE_AFTER_MONITOR_INIT");

    monitor_reset(&mon);

    float* ft8_buffer = (float*)heap_caps_malloc(sizeof(float) * mon.block_size, MALLOC_CAP_DEFAULT);
    float* temp_dec = (float*)heap_caps_malloc(sizeof(float) * 512, MALLOC_CAP_DEFAULT);
    log_heap("AUDIO_PIPE_AFTER_SAMPLE_BUFFERS");

    if (!ft8_buffer || !temp_dec) {
        ESP_LOGE(tag, "pipeline buffer allocation failed");
        if (ft8_buffer) free(ft8_buffer);
        if (temp_dec) free(temp_dec);
        monitor_free(&mon);
        return;
    }

    const int64_t slot_ms      = g_protocol->slot_time_ms;
    const int     target_blocks = g_protocol->total_symbols + 1;
    const uint32_t sym_delay_ms = (uint32_t)(g_protocol->symbol_period * 1000.0f + 0.5f);

    int64_t now_ms = rtc_now_ms();
    int64_t rem = now_ms % slot_ms;
    int64_t wait_ms = (rem < 100) ? 0 : (slot_ms - rem);
    if (wait_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)wait_ms));
    }

    int ft8_buffer_idx = 0;
    TickType_t next_wake = xTaskGetTickCount();
    int slot_blocks = 0;
    int64_t slot_idx = rtc_now_ms() / slot_ms;
    int64_t slot_start_ms = slot_idx * slot_ms;
    (void)slot_start_ms;

    while (!cfg->should_stop(cfg->ctx)) {
        int samples_dec = cfg->read(cfg->ctx, temp_dec, 512);
        if (samples_dec <= 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        if (samples_dec > 512) samples_dec = 512;

        for (int i = 0; i < samples_dec && !cfg->should_stop(cfg->ctx); i++) {
            ft8_buffer[ft8_buffer_idx++] = temp_dec[i];

            if (ft8_buffer_idx >= mon.block_size) {
                double acc = 0.0;
                for (int j = 0; j < mon.block_size; ++j) {
                    acc += fabsf(ft8_buffer[j]);
                }
                float level = (float)(acc / mon.block_size);
                float gain = (level > 1e-6f) ? 0.1f / level : 1.0f;
                if (gain < 0.1f) gain = 0.1f;
                if (gain > 10.0f) gain = 10.0f;
                for (int j = 0; j < mon.block_size; ++j) {
                    ft8_buffer[j] *= gain;
                }

                if (mon.wf.num_blocks < target_blocks) {
                    monitor_process(&mon, ft8_buffer);
                    push_waterfall_latest(mon);
                }

                if (cfg->on_block_processed) {
                    cfg->on_block_processed(cfg->ctx);
                }

                ft8_buffer_idx = 0;
                vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(sym_delay_ms));

                slot_blocks++;
                int64_t now_idx = rtc_now_ms() / slot_ms;
                if (now_idx != slot_idx) {
                    ESP_LOGI(tag, "Slot boundary %lld->%lld blocks=%d wf=%d",
                             (long long)slot_idx, (long long)now_idx,
                             slot_blocks, mon.wf.num_blocks);
                    if (slot_idx > g_decode_applied_slot_idx) {
                        g_decode_applied_slot_idx = slot_idx;
                    }
                    slot_idx = now_idx;
                    slot_start_ms = slot_idx * slot_ms;
                    slot_blocks = 0;
                    mon.wf.num_blocks = 0;
                    monitor_reset(&mon);
                    next_wake = xTaskGetTickCount();
                } else if (slot_blocks >= g_protocol->total_symbols &&
                           mon.wf.num_blocks >= g_protocol->total_symbols) {
                    ESP_LOGI(tag, "Triggering decode at slot %lld blocks=%d wf=%d",
                             (long long)slot_idx, slot_blocks, mon.wf.num_blocks);
                    if (g_tx_active || g_was_txing) {
                        ESP_LOGI(tag, "Skipping decode on TX slot %lld",
                                 (long long)slot_idx);
                        if (slot_idx > g_decode_applied_slot_idx) {
                            g_decode_applied_slot_idx = slot_idx;
                        }
                    } else if (g_decode_enabled) {
                        g_decode_slot_idx = slot_idx;
                        g_decode_in_progress = true;
                        decode_monitor_results(&mon, &mon_cfg, false);
                    } else {
                        ESP_LOGI(tag, "Decode paused; skipping");
                        if (slot_idx > g_decode_applied_slot_idx) {
                            g_decode_applied_slot_idx = slot_idx;
                        }
                    }
                    monitor_reset(&mon);
                    mon.wf.num_blocks = 0;
                    slot_blocks = 0;
                    next_wake = xTaskGetTickCount();
                }
            }
        }
    }

    free(ft8_buffer);
    free(temp_dec);
    monitor_free(&mon);
}
