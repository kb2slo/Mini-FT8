#include "porta.h"

#include <cstring>
#include <string>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "main_services.h"
#include "sidekick_flasher.h"

namespace {

constexpr uart_port_t kPortaUart = UART_NUM_1;
constexpr gpio_num_t kPortaRxPin = GPIO_NUM_1;
constexpr gpio_num_t kPortaTxPin = GPIO_NUM_2;

// Fixed. The sidekick only ever speaks 115200 (sidekick/main/main.c), and with
// GPS off this port there is nothing else on the wire to probe for.
constexpr int kBaud = 115200;

constexpr uint8_t kCompanionSync = 0xC6;
// sidekick's beacon (RFC 0001 §5.2c): sync + version[32] + XOR checksum,
// matching sidekick/main/main.c's PORTA_* frame layout exactly.
constexpr size_t kCompanionVersionLen = 32;
constexpr size_t kCompanionTailLen = kCompanionVersionLen + 1;  // version + checksum
// 34 bytes at 115200 baud is ~3 ms; a partial frame sitting this long means a
// bit error corrupted it, not that the rest is still arriving.
constexpr uint32_t kCompanionFrameTimeoutMs = 250;

const char* kTag = "PORTA";

bool s_running = false;
bool s_collecting = false;
uint8_t s_buf[kCompanionTailLen];
size_t s_have = 0;
uint32_t s_collect_start_ms = 0;
char s_version[kCompanionVersionLen + 1] = {};

inline uint32_t now_ms() {
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void reset_collection() {
  s_collecting = false;
  s_have = 0;
}

bool configure_uart() {
  uart_config_t cfg = {};
  cfg.baud_rate = kBaud;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
#ifdef UART_SCLK_REF_TICK
  cfg.source_clk = UART_SCLK_REF_TICK;
#else
  cfg.source_clk = UART_SCLK_DEFAULT;
#endif
  if (uart_param_config(kPortaUart, &cfg) != ESP_OK) return false;
  if (uart_set_pin(kPortaUart, kPortaTxPin, kPortaRxPin,
                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    return false;
  }
  uart_flush_input(kPortaUart);
  return true;
}

// Validates a completed beacon (sync already consumed; s_buf holds
// version[32] + checksum) and compares the version against this ADV's own
// embedded sidekick build (RFC 0001 §5.2b/§5.2c) — the same comparison
// sidekick_flasher makes over USB-C, reached without a USB-C session. A
// checksum failure is line noise, not a companion: discard and resume.
void try_complete_frame() {
  uint8_t checksum = kCompanionSync;
  for (size_t i = 0; i < kCompanionVersionLen; ++i) checksum ^= s_buf[i];
  if (checksum != s_buf[kCompanionVersionLen]) {
    ESP_LOGW(kTag, "Companion beacon checksum mismatch, discarding");
    reset_collection();
    return;
  }

  memcpy(s_version, s_buf, kCompanionVersionLen);
  s_version[kCompanionVersionLen] = '\0';
  reset_collection();

  char local_version[kCompanionVersionLen + 1] = {};
  const bool matches =
      sidekick_flasher_embedded_version(local_version, sizeof(local_version)) &&
      strncmp(s_version, local_version, kCompanionVersionLen) == 0;

  if (matches) {
    ESP_LOGI(kTag, "Companion version matches: %s", s_version);
    // One line, not two — the log viewer jumps to whichever page the newest
    // line landed on, so two related lines can straddle a page boundary and
    // only the second is ever seen. Bench-confirmed 2026-09-04.
    debug_log_line_public(std::string("OK ") + s_version);
  } else {
    ESP_LOGW(kTag, "Companion version MISMATCH: remote=%s local=%s",
             s_version, local_version);
    debug_log_line_public(std::string("X R:") + s_version);
    debug_log_line_public(std::string("X L:") + local_version);
  }
}

// One beacon per second, so this is a trickle. Sync is only meaningful while
// idle: once collecting, every byte is frame content, because 0xC6 can occur
// inside a version string's bytes as readily as anywhere else.
void ingest(const uint8_t* data, int len) {
  for (int i = 0; i < len; ++i) {
    const uint8_t raw = data[i];
    if (s_collecting) {
      s_buf[s_have++] = raw;
      if (s_have >= kCompanionTailLen) try_complete_frame();
      continue;
    }
    if (raw == kCompanionSync) {
      s_collecting = true;
      s_have = 0;
      s_collect_start_ms = now_ms();
    }
  }
}

}  // namespace

void porta_start() {
  if (s_running) return;

  gpio_reset_pin(kPortaTxPin);
  gpio_reset_pin(kPortaRxPin);

  esp_err_t err = uart_driver_install(kPortaUart, 2048, 0, 0, nullptr, 0);
  if (err == ESP_ERR_INVALID_STATE) {
    uart_driver_delete(kPortaUart);
    err = uart_driver_install(kPortaUart, 2048, 0, 0, nullptr, 0);
  }
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "UART driver install failed: %d", (int)err);
    return;
  }
  if (!configure_uart()) {
    ESP_LOGW(kTag, "UART config failed");
    uart_driver_delete(kPortaUart);
    return;
  }

  reset_collection();
  s_version[0] = '\0';
  s_running = true;
  ESP_LOGI(kTag, "Started on UART%d TX=G%d RX=G%d baud=%d",
           (int)kPortaUart, (int)kPortaTxPin, (int)kPortaRxPin, kBaud);
}

void porta_stop() {
  if (!s_running) return;
  uart_driver_delete(kPortaUart);
  reset_collection();
  s_running = false;
  ESP_LOGI(kTag, "Stopped");
}

void porta_tick() {
  if (!s_running) return;

  uint8_t buf[128];
  const int len = uart_read_bytes(kPortaUart, buf, sizeof(buf), 0);
  if (len > 0) {
    ingest(buf, len);
  }

  // Abandon a frame that stopped arriving mid-way rather than holding the
  // collector open against the next beacon's sync byte.
  if (s_collecting && now_ms() - s_collect_start_ms > kCompanionFrameTimeoutMs) {
    ESP_LOGW(kTag, "Companion beacon timed out mid-frame, discarding");
    reset_collection();
  }
}
