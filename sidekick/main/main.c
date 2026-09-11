#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "porta_proto.h"
#include "wifi_prov.h"

static const char *TAG = "sidekick";

// PORTA companion beacon — RFC 0001 §5.2c. One-way, sidekick -> ADV: sync
// byte (never collides with NMEA's '$'-prefixed sentences, so the ADV can
// tell GPS and companion apart by listening) + this build's version string
// (from esp_app_desc_t, the same git-SHA+dirty value tools/git_version.cmake
// computes for both sidekick and the ADV, so an exact string match is
// meaningful) + a simple checksum so the ADV isn't locking onto a single
// stray byte of line noise as "companion present".
//
// Grove pin roles mirror the ADV's PORTA wiring: a Grove cable is
// straight-through (G1-to-G1, G2-to-G2), and the ADV receives on its G1 /
// transmits on its G2, so sidekick must transmit on G1 / receive on G2 for
// the two ends to actually reach each other over the same two wires.
#define PORTA_UART UART_NUM_1
#define PORTA_TX_PIN GPIO_NUM_1  // G1
#define PORTA_RX_PIN GPIO_NUM_2  // G2
#define PORTA_BAUD 115200
#define PORTA_SYNC_BYTE 0xC6
#define PORTA_VERSION_LEN 32
#define PORTA_FRAME_LEN (1 + PORTA_VERSION_LEN + 1)  // sync + version + checksum
#define PORTA_BEACON_INTERVAL_MS 1000
#define PORTA_RX_BUF 2048

static void porta_beacon_init(void) {
    gpio_reset_pin(PORTA_TX_PIN);
    gpio_reset_pin(PORTA_RX_PIN);

    uart_config_t cfg = {
        .baud_rate = PORTA_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // RX was 256, which is smaller than one maximum frame (259 bytes) -- a
    // single full decode event could not fit, let alone a burst. The host sends
    // at most one frame per tick, but bursts arrive while this task is asleep.
    ESP_ERROR_CHECK(uart_driver_install(PORTA_UART, PORTA_RX_BUF, 256, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(PORTA_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(PORTA_UART, PORTA_TX_PIN, PORTA_RX_PIN,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void porta_beacon_send(void) {
    const esp_app_desc_t *desc = esp_app_get_description();

    uint8_t frame[PORTA_FRAME_LEN];
    frame[0] = PORTA_SYNC_BYTE;
    memset(&frame[1], 0, PORTA_VERSION_LEN);
    strncpy((char *)&frame[1], desc->version, PORTA_VERSION_LEN - 1);

    uint8_t checksum = 0;
    for (int i = 0; i < 1 + PORTA_VERSION_LEN; ++i) {
        checksum ^= frame[i];
    }
    frame[1 + PORTA_VERSION_LEN] = checksum;

    uart_write_bytes(PORTA_UART, (const char *)frame, sizeof(frame));
}

// PORTA update flash (RFC 0001 §5.2c "Update flash over PORTA") — tried a
// button-hold self-restart (hold the on-board GPIO9 button, sidekick calls
// esp_restart() on its own), reasoning the operator's finger holding GPIO9
// low at that exact reset would land the chip in its ROM bootloader the
// same as a fresh cable plug-in. Bench-tested 2026-09-04: the button read
// and the restart both worked correctly, but the reboot banner showed
// `boot:0xd (SPI_FAST_FLASH_BOOT)` — a software (`SW_CPU`) reset does not
// cause the ROM to re-sample GPIO9 at all, so it boots straight back into
// the app regardless of the button. Corroborated by Espressif's own esptool
// disabling its (different, more aggressive) watchdog-reset trick
// specifically on ESP32-C6 for causing full system freezes — this looks
// like a genuine chip limitation, not a timing/implementation bug. Removed
// rather than left in as dead, misleading UI. Next real attempt: a custom
// second-stage bootloader that checks an RTC-memory flag and, if set, runs
// its own flash-receiver instead of the app — sidesteps ROM strap-sampling
// entirely, needs no wiring, but is real bootloader-level engineering
// (higher stakes: bugs there affect every boot, not just updates) — see
// RFC 0001 §5.2's "Future phase" note. USB-C stays the only flash path
// until that's built.

// With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE an image installed by OTA boots
// in PENDING_VERIFY and is rolled back on the next restart unless it declares
// itself good. "Good" here means the companion link works — an image that
// boots but cannot beacon is useless to the ADV, so the beacon is the check
// rather than merely reaching app_main(). Images written over USB-C are not
// pending and this is a no-op for them.
static void mark_valid_once_beaconing(uint32_t beacons_sent)
{
    static bool done = false;
    if (done || beacons_sent < 3) {
        return;
    }
    done = true;
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGI(TAG, "Image marked valid (beacon confirmed); rollback cancelled");
    } else {
        ESP_LOGE(TAG, "Could not mark image valid — it will roll back on restart");
    }
}

// Inbound events from the host (I28a). Its own task rather than a poll in the
// 1 Hz beacon loop: events arrive continuously and a second of latency would
// make a live log viewer useless, quite apart from overrunning the RX buffer.
//
// For now these are printed on this device's USB-C -- which is the point. The
// ADV's own USB-C is a USB host for the radio whenever the radio is attached,
// so it has no console exactly when one is most wanted. This port is idle then.
static void porta_rx_task(void *arg)
{
    (void)arg;
    porta_decoder_t dec;
    porta_decoder_init(&dec);

    uint8_t buf[256];
    porta_frame_t frame;
    char text[PORTA_EVENT_TEXT_MAX + 1];
    porta_decode_event_t ev;

    while (1) {
        // Blocks this task, not the beacon: a short timeout keeps latency low
        // without spinning.
        const int n = uart_read_bytes(PORTA_UART, buf, sizeof(buf), pdMS_TO_TICKS(50));
        for (int i = 0; i < n; ++i) {
            if (!porta_decoder_push(&dec, buf[i], &frame)) {
                continue;
            }
            if (porta_proto_parse_log(&frame, text)) {
                ESP_LOGI("HOST", "%s", text);
            } else if (porta_proto_parse_decode(&frame, &ev)) {
                ESP_LOGI("HOST", "decode %+d dB %4u Hz %+.2f s %s%s%s",
                         ev.snr, ev.offset_hz, ev.dt_centis / 100.0,
                         ev.is_to_me ? "[me] " : "",
                         ev.is_cq ? "[cq] " : "",
                         ev.text);
            } else {
                ESP_LOGW("HOST", "frame type 0x%02x len %u (no handler yet)",
                         frame.type, frame.len);
            }
        }
        if (dec.crc_errors) {
            static uint32_t reported;
            if (dec.crc_errors != reported) {
                reported = dec.crc_errors;
                ESP_LOGW("HOST", "link: %" PRIu32 " CRC errors, %" PRIu32 " frames ok",
                         dec.crc_errors, dec.frames_ok);
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Mini-FT8 sidekick booting (IDF %s)", esp_get_idf_version());

    // Beacon first, WiFi second. The PORTA companion link is what the ADV
    // depends on; a sidekick that cannot reach WiFi is degraded, not broken,
    // so nothing about network bring-up is allowed to delay or block it.
    porta_beacon_init();
    xTaskCreate(porta_rx_task, "porta_rx", 4096, NULL, 5, NULL);
    wifi_prov_start();

    if (wifi_prov_is_online()) {
        ESP_LOGI(TAG, "WiFi: online on '%s'", wifi_prov_ssid());
    } else if (wifi_prov_ap_active()) {
        ESP_LOGW(TAG, "WiFi: provisioning AP up, not joined");
    }

    uint32_t heartbeat = 0;
    while (1) {
        porta_beacon_send();
        mark_valid_once_beaconing(heartbeat);
        if (heartbeat % 5 == 0) {
            ESP_LOGI(TAG, "alive: %" PRIu32 " wifi=%s", heartbeat / 5,
                     wifi_prov_is_online() ? "up" : (wifi_prov_ap_active() ? "ap" : "down"));
        }
        ++heartbeat;
        vTaskDelay(pdMS_TO_TICKS(PORTA_BEACON_INTERVAL_MS));
    }
}
