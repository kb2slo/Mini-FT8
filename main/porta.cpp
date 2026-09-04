#include "porta.h"

#include <cstring>
#include <string>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gps.h"
#include "nano_flasher.h"

// ADV's own USB-C is a USB host while Mini-FT8 runs (QMX/Nano support), so
// it never presents a serial console — ESP_LOG output here isn't reachable
// without a G4/G5 USB-TTL adapter. debug_log_line_public() writes the
// on-screen debug ring buffer (DEBUG UI mode) instead, same fix used for
// nano_flasher's bring-up (RFC 0001 §5.2b).
extern void debug_log_line_public(const std::string& msg);

namespace {

constexpr uart_port_t kPortaUart = UART_NUM_1;
constexpr gpio_num_t kPortaRxPin = GPIO_NUM_1;
constexpr gpio_num_t kPortaTxPin = GPIO_NUM_2;
constexpr int kBaudFast = 115200;
constexpr int kBaudSlow = 9600;
constexpr size_t kLineMax = 128;
// Mirrors gps.cpp's own probe window: how long to wait for a decodable
// frame at one baud before trying the other.
constexpr uint32_t kProbeWindowMs = 2500;
// How long a locked role can go silent before re-arming to kUnknown. PORTA
// has no physical presence signal, so "gone quiet" is the only way to
// notice the operator swapped the physical device. GPS sentences are
// typically ~1 Hz; generous on purpose to tolerate a rough antenna spell
// without flapping. Untuned — first field pass.
constexpr uint32_t kReArmSilenceMs = 10000;
constexpr uint8_t kCompanionSync = 0xC6;
// sidekick's beacon (RFC 0001 §5.2c): sync + version[32] + XOR checksum,
// matching sidekick/main/main.c's PORTA_* frame layout exactly.
constexpr size_t kCompanionVersionLen = 32;
constexpr size_t kCompanionTailLen = kCompanionVersionLen + 1;  // version + checksum
// 34 bytes at 115200 baud is ~3ms; a partial frame sitting this long means
// a bit error corrupted it, not that the rest is still arriving. Abandon
// and resume scanning rather than blocking GPS detection indefinitely.
constexpr uint32_t kCompanionFrameTimeoutMs = 250;

const char* kTag = "PORTA";

PortaRole s_role = PortaRole::kUnknown;
bool s_running = false;
int s_hint_baud = kBaudFast;
int s_probe_baud = kBaudFast;
uint32_t s_probe_start_ms = 0;
uint32_t s_probe_rx_bytes = 0;
uint32_t s_last_rx_ms = 0;
std::string s_line_buffer;

bool s_collecting_companion = false;
uint8_t s_companion_buf[kCompanionTailLen];
size_t s_companion_have = 0;
uint32_t s_companion_start_ms = 0;
char s_companion_version[kCompanionVersionLen + 1] = {};
bool s_companion_version_matches = false;

inline uint32_t now_ms() {
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

int normalize_baud(int baud) {
  return (baud == kBaudSlow) ? kBaudSlow : kBaudFast;
}

int other_baud(int baud) {
  return (normalize_baud(baud) == kBaudFast) ? kBaudSlow : kBaudFast;
}

bool configure_uart(int baud) {
  uart_config_t cfg = {};
  cfg.baud_rate = normalize_baud(baud);
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
  if (uart_set_baudrate(kPortaUart, normalize_baud(baud)) != ESP_OK) return false;
  uart_flush_input(kPortaUart);
  return true;
}

void reset_companion_collection() {
  s_collecting_companion = false;
  s_companion_have = 0;
}

void rearm_probe_window() {
  s_probe_start_ms = now_ms();
  s_probe_rx_bytes = 0;
  s_line_buffer.clear();
  reset_companion_collection();
}

void rearm_unknown() {
  if (s_role != PortaRole::kUnknown) {
    // Screen is 240px / 20 chars per row at this text size (ui.cpp's
    // RX_LINES/SCREEN_W) — ui_draw_list() doesn't truncate or disable
    // wrap, so a longer line garbles into the row below it. Keep these
    // short; found the hard way (2026-09-04 field test).
    debug_log_line_public("PORTA: re-arm");
  }
  if (s_role == PortaRole::kGps) {
    gps_stop();
  }
  ESP_LOGI(kTag, "Role re-armed to Unknown (silence)");
  s_role = PortaRole::kUnknown;
  s_probe_baud = s_hint_baud;
  configure_uart(s_probe_baud);
  rearm_probe_window();
  s_companion_version[0] = '\0';
  s_companion_version_matches = false;
}

void lock_role(PortaRole role) {
  s_role = role;
  s_last_rx_ms = now_ms();
  const char* name = (role == PortaRole::kGps) ? "GPS" : "Companion";
  ESP_LOGI(kTag, "Role locked: %s at baud=%d", name, s_probe_baud);
  if (role == PortaRole::kGps) {
    debug_log_line_public("PORTA: GPS");
  }
}

// Validates a completed sidekick beacon (sync already consumed by the
// caller; s_companion_buf holds version[32] + checksum) and, if the XOR
// checksum matches, locks kCompanion and compares against this ADV's own
// embedded sidekick build (RFC 0001 §5.2b/§5.2c — same version-comparison
// data nano_flasher already uses for the USB-C path, just reached without
// a USB-C session). A checksum failure is treated as line noise, not a
// companion — discarded, scanning resumes rather than locking on a
// corrupted frame.
void try_complete_companion_frame() {
  uint8_t checksum = kCompanionSync;
  for (size_t i = 0; i < kCompanionVersionLen; ++i) checksum ^= s_companion_buf[i];
  if (checksum != s_companion_buf[kCompanionVersionLen]) {
    ESP_LOGW(kTag, "Companion beacon checksum mismatch, discarding");
    reset_companion_collection();
    return;
  }

  memcpy(s_companion_version, s_companion_buf, kCompanionVersionLen);
  s_companion_version[kCompanionVersionLen] = '\0';
  reset_companion_collection();

  char local_version[kCompanionVersionLen + 1] = {};
  s_companion_version_matches =
      nano_flasher_embedded_version(local_version, sizeof(local_version)) &&
      strncmp(s_companion_version, local_version, kCompanionVersionLen) == 0;

  lock_role(PortaRole::kCompanion);
  if (s_companion_version_matches) {
    ESP_LOGI(kTag, "Companion version matches: %s", s_companion_version);
    // Split across rows — see the width note in rearm_unknown().
    debug_log_line_public("PORTA: Sidekick OK");
    debug_log_line_public(s_companion_version);
  } else {
    ESP_LOGW(kTag, "Companion version MISMATCH: remote=%s local=%s",
             s_companion_version, local_version);
    debug_log_line_public("PORTA: Sidekick STALE");
    debug_log_line_public(std::string("R:") + s_companion_version);
    debug_log_line_public(std::string("L:") + local_version);
  }
}

// Scans bytes while s_role == kUnknown for either a checksum-valid NMEA
// line (-> GPS) or a validated companion beacon (-> Companion). GPS
// detection and companion detection are both now full-frame-validated
// (NMEA checksum; beacon XOR checksum) — neither locks on a single
// unvalidated byte. Returns once a role locks; caller should stop feeding
// this function once it does. Any bytes still left in the current chunk
// past the lock point are dropped rather than re-routed — at most one
// transient NMEA sentence or partial companion frame, self-healing on the
// next ~1 Hz GPS sentence or next-second companion beacon. Not worth the
// complexity to avoid for a one-time, per-role-lock event.
void ingest_unknown(const uint8_t* data, int len) {
  for (int i = 0; i < len && s_role == PortaRole::kUnknown; ++i) {
    const uint8_t raw = data[i];

    if (s_collecting_companion) {
      s_companion_buf[s_companion_have++] = raw;
      if (s_companion_have >= kCompanionTailLen) {
        try_complete_companion_frame();
      }
      continue;
    }

    if (raw == kCompanionSync) {
      s_collecting_companion = true;
      s_companion_have = 0;
      s_companion_start_ms = now_ms();
      s_line_buffer.clear();  // 0xC6 can't be part of a printable NMEA line
      continue;
    }

    const char c = (char)raw;
    if (c == '\r' || c == '\n') {
      if (!s_line_buffer.empty() && gps_is_valid_nmea_line(s_line_buffer)) {
        gps_start_fed(s_probe_baud);
        gps_ingest(reinterpret_cast<const uint8_t*>(s_line_buffer.data()),
                   (int)s_line_buffer.size());
        const char nl = '\n';
        gps_ingest(reinterpret_cast<const uint8_t*>(&nl), 1);
        lock_role(PortaRole::kGps);
        return;
      }
      s_line_buffer.clear();
      continue;
    }
    if ((unsigned char)c < 32 || (unsigned char)c > 126) continue;
    s_line_buffer.push_back(c);
    if (s_line_buffer.size() > kLineMax) s_line_buffer.clear();
  }

  if (s_collecting_companion && (now_ms() - s_companion_start_ms) > kCompanionFrameTimeoutMs) {
    ESP_LOGW(kTag, "Companion beacon timed out mid-frame, discarding");
    reset_companion_collection();
  }
}

}  // namespace

void porta_start(int gps_baud_hint) {
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

  s_hint_baud = normalize_baud(gps_baud_hint);
  s_probe_baud = s_hint_baud;
  if (!configure_uart(s_probe_baud)) {
    ESP_LOGW(kTag, "UART config failed");
    uart_driver_delete(kPortaUart);
    return;
  }

  s_role = PortaRole::kUnknown;
  s_running = true;
  rearm_probe_window();
  ESP_LOGI(kTag, "Started on UART%d TX=G%d RX=G%d hint_baud=%d",
           (int)kPortaUart, (int)kPortaTxPin, (int)kPortaRxPin, s_hint_baud);
}

void porta_stop() {
  if (!s_running) return;
  if (s_role == PortaRole::kGps) {
    gps_stop();
  }
  uart_flush_input(kPortaUart);
  uart_driver_delete(kPortaUart);
  s_running = false;
  s_role = PortaRole::kUnknown;
  s_line_buffer.clear();
  s_companion_version[0] = '\0';
  s_companion_version_matches = false;
  ESP_LOGI(kTag, "Stopped");
}

void porta_tick() {
  if (!s_running) return;

  uint8_t buf[256];
  int len = uart_read_bytes(kPortaUart, buf, sizeof(buf), 0);

  switch (s_role) {
    case PortaRole::kGps:
      if (len > 0) {
        s_last_rx_ms = now_ms();
        gps_ingest(buf, len);
      }
      if (now_ms() - s_last_rx_ms > kReArmSilenceMs) rearm_unknown();
      return;

    case PortaRole::kCompanion:
      // The lock-time frame is fully validated (checksum); re-validating
      // every subsequent ~1s beacon while already locked (e.g. to notice
      // sidekick got reflashed to a different version mid-session without
      // a PORTA disconnect) is a reasonable next step, not built here —
      // liveness alone (any bytes at all) is enough to know the same
      // physical device is still present and keep the re-arm timer honest.
      if (len > 0) s_last_rx_ms = now_ms();
      if (now_ms() - s_last_rx_ms > kReArmSilenceMs) rearm_unknown();
      return;

    case PortaRole::kUnknown:
      if (len > 0) {
        s_probe_rx_bytes += (uint32_t)len;
        ingest_unknown(buf, len);
      }
      if (s_role == PortaRole::kUnknown && s_probe_rx_bytes > 0 &&
          (now_ms() - s_probe_start_ms) >= kProbeWindowMs) {
        s_probe_baud = other_baud(s_probe_baud);
        configure_uart(s_probe_baud);
        rearm_probe_window();
        ESP_LOGI(kTag, "No frame recognized, probing baud=%d", s_probe_baud);
      }
      return;
  }
}

PortaRole porta_get_role() {
  return s_role;
}

const char* porta_get_companion_version() {
  return s_companion_version;
}

bool porta_companion_version_matches() {
  return s_companion_version_matches;
}
