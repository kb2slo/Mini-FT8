#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
    ESP_ERROR_CHECK(uart_driver_install(PORTA_UART, 256, 256, 0, NULL, 0));
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

void app_main(void)
{
    ESP_LOGI(TAG, "Mini-FT8 sidekick booting (IDF %s)", esp_get_idf_version());

    porta_beacon_init();

    uint32_t heartbeat = 0;
    while (1) {
        porta_beacon_send();
        if (heartbeat % 5 == 0) {
            ESP_LOGI(TAG, "alive: %" PRIu32, heartbeat / 5);
        }
        ++heartbeat;
        vTaskDelay(pdMS_TO_TICKS(PORTA_BEACON_INTERVAL_MS));
    }
}
