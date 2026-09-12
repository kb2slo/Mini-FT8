#include "porta.h"

#include <cinttypes>
#include <cstring>
#include <string>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "main_services.h"

// Both defined in main.cpp: the clock this stamps events with, and the setter
// the control direction reaches. The setter returns nullptr on success or a
// short reason for the operator's log.
int64_t rtc_now_ms();
const char* porta_host_set_clock(uint32_t epoch_secs, uint16_t millis);
#include "porta_proto.h"
#include "sidekick_flasher.h"

namespace {

constexpr uart_port_t kPortaUart = UART_NUM_1;
constexpr gpio_num_t kPortaRxPin = GPIO_NUM_1;
constexpr gpio_num_t kPortaTxPin = GPIO_NUM_2;

// Fixed. The sidekick only ever speaks 115200 (sidekick/main/main.c), and with
// GPS off this port there is nothing else on the wire to probe for.
constexpr int kBaud = 115200;

// Big enough that one frame per tick never finds it full; see drain_out().
constexpr int kTxBufBytes = 4096;

const char* kTag = "PORTA";

bool s_running = false;
porta_decoder_t s_dec;
char s_version[PORTA_HELLO_VERSION_LEN + 1] = {};

// Enough to miss two HELLOs at the 5 s cadence before concluding anything, so
// one dropped frame or a moment of noise does not raise a prompt.
constexpr uint32_t kCompanionQuietMs = 15000;
uint32_t s_last_byte_ms = 0;    // anything at all arrived
uint32_t s_last_frame_ms = 0;   // a valid frame arrived
bool s_version_matches = false;

inline uint32_t now_ms() {
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
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

// Outbound queue. Sized for a busy slot: roughly 30 decodes plus whatever the
// log produces, at 259 bytes worst case. 24 entries is ~6 KB, which is cheap
// against the alternative of dropping most of a slot's decodes.
constexpr size_t kOutQueueLen = 24;

struct OutFrame {
  uint8_t bytes[PORTA_PROTO_MAX_FRAME];
  size_t len;
};

OutFrame s_out[kOutQueueLen];
size_t s_out_head = 0;   // next to send
size_t s_out_count = 0;
uint32_t s_dropped = 0;

// Drops the oldest rather than the newest. On a link that has fallen behind,
// the recent decodes are the ones worth having; discarding them to preserve a
// backlog would make the viewer lag further the busier the band got.
void enqueue(const uint8_t* bytes, size_t len) {
  if (len == 0 || len > PORTA_PROTO_MAX_FRAME) return;
  if (s_out_count == kOutQueueLen) {
    s_out_head = (s_out_head + 1) % kOutQueueLen;
    s_out_count--;
    s_dropped++;
  }
  const size_t slot = (s_out_head + s_out_count) % kOutQueueLen;
  memcpy(s_out[slot].bytes, bytes, len);
  s_out[slot].len = len;
  s_out_count++;
}

// One frame per tick, which is the rate limit that keeps the TX ring from ever
// filling. A full frame is 259 bytes and the line carries ~11 KB/s, so even at
// a 50 Hz tick this emits under 13 KB/s of worst-case frames into a 4 KB ring
// that drains continuously -- and real traffic is far below that, since a busy
// slot's decodes arrive spread over fifteen seconds. Bursts are absorbed by the
// queue, not by writing harder.
constexpr int kMaxSendPerTick = 1;

void drain_out() {
  for (int i = 0; i < kMaxSendPerTick && s_out_count > 0; ++i) {
    const OutFrame& f = s_out[s_out_head];
    uart_write_bytes(kPortaUart, (const char*)f.bytes, f.len);
    s_out_head = (s_out_head + 1) % kOutQueueLen;
    s_out_count--;
  }
}

// HELLO replaces the ad-hoc beacon. Same purpose -- report the companion's
// build so the operator learns, without a USB-C session, whether it matches the
// image this ADV carries (RFC 0001 §5.2b/§5.2c) -- but it now arrives framed
// and CRC-checked like everything else, and carries a protocol version the old
// beacon had no room for.
void handle_hello(const porta_frame_t& f) {
  uint8_t proto = 0;
  char remote[PORTA_HELLO_VERSION_LEN + 1] = {};
  if (!porta_proto_parse_hello(&f, &proto, remote)) return;

  if (proto != PORTA_PROTO_VERSION) {
    // Said out loud rather than failing obscurely later: the two ends are
    // flashed independently, so a mismatch is a normal state mid-update.
    ESP_LOGW(kTag, "Companion speaks protocol v%u, we speak v%u", proto,
             (unsigned)PORTA_PROTO_VERSION);
  }

  if (strncmp(remote, s_version, PORTA_HELLO_VERSION_LEN) == 0) {
    return;   // unchanged; HELLO repeats every few seconds
  }
  strncpy(s_version, remote, sizeof(s_version) - 1);

  char local_version[PORTA_HELLO_VERSION_LEN + 1] = {};
  const bool matches =
      sidekick_flasher_embedded_version(local_version, sizeof(local_version)) &&
      strncmp(remote, local_version, PORTA_HELLO_VERSION_LEN) == 0;

  s_version_matches = matches;
  if (matches) {
    ESP_LOGI(kTag, "Companion version matches: %s", remote);
    // One line, not two -- the log viewer jumps to whichever page the newest
    // line landed on, so two related lines can straddle a page boundary and
    // only the second is ever seen. Bench-confirmed 2026-09-04.
    debug_log_line_public(std::string("OK ") + remote);
  } else {
    ESP_LOGW(kTag, "Companion version MISMATCH: remote=%s local=%s", remote, local_version);
    debug_log_line_public(std::string("X R:") + remote);
    debug_log_line_public(std::string("X L:") + local_version);
  }
}

// The control direction. Answered with ACK or NAK carrying the same verb, so
// the sidekick can match a reply to its request without a sequence number.
void handle_action(const porta_frame_t& f) {
  if (f.len < 1) return;
  uint8_t buf[PORTA_PROTO_MAX_FRAME];
  const uint8_t verb = f.payload[0];

  if (verb == PORTA_ACT_SET_CLOCK) {
    uint32_t secs = 0;
    uint16_t ms = 0;
    if (!porta_proto_parse_set_clock(&f, &secs, &ms)) {
      enqueue(buf, porta_proto_encode_nak(verb, "malformed", buf, sizeof(buf)));
      return;
    }
    const char* why = porta_host_set_clock(secs, ms);
    if (why) {
      enqueue(buf, porta_proto_encode_nak(verb, why, buf, sizeof(buf)));
    } else {
      enqueue(buf, porta_proto_encode_ack(verb, buf, sizeof(buf)));
    }
    return;
  }

  enqueue(buf, porta_proto_encode_nak(verb, "unknown action", buf, sizeof(buf)));
}

void handle_frame(const porta_frame_t& f) {
  switch (f.type) {
  case PORTA_MSG_HELLO:  handle_hello(f);  break;
  case PORTA_MSG_ACTION: handle_action(f); break;
  default:
    ESP_LOGD(kTag, "Unhandled frame type 0x%02x len %u", f.type, f.len);
    break;
  }
}

}  // namespace

// Stamped at emit, not at arrival. The queue can hold events through a burst,
// and a viewer stamping on arrival would pile a whole slot's decodes onto
// whatever second they happened to drain.
uint32_t host_epoch_secs() {
  const int64_t ms = rtc_now_ms();
  // Before the clock is set the soft RTC reads as an implausible epoch. Zero
  // means "unknown" on the wire, which the viewer renders as blank rather than
  // as a 1970 timestamp that looks like data.
  const int64_t secs = ms / 1000;
  return (secs > 1000000000LL) ? (uint32_t)secs : 0u;
}

void porta_emit_log(const char* text) {
  if (!s_running || !text) return;
  uint8_t buf[PORTA_PROTO_MAX_FRAME];
  const size_t n = porta_proto_encode_log(host_epoch_secs(), text, buf, sizeof(buf));
  enqueue(buf, n);
}

void porta_emit_decode(const char* text, int snr, int offset_hz, float dt_s,
                       bool is_cq, bool is_to_me, bool is_recent_qso) {
  if (!s_running || !text) return;
  porta_decode_event_t ev = {};
  ev.epoch_secs = host_epoch_secs();
  strncpy(ev.text, text, sizeof(ev.text) - 1);
  // Clamped rather than cast: an out-of-range value should read as an extreme,
  // not wrap round to a plausible-looking wrong one.
  ev.snr = (int8_t)(snr < -128 ? -128 : (snr > 127 ? 127 : snr));
  ev.offset_hz = (uint16_t)(offset_hz < 0 ? 0 : (offset_hz > 65535 ? 65535 : offset_hz));
  const long centis = (long)(dt_s * 100.0f);
  ev.dt_centis = (int16_t)(centis < -32768 ? -32768 : (centis > 32767 ? 32767 : centis));
  ev.is_cq = is_cq;
  ev.is_to_me = is_to_me;
  ev.is_recent_qso = is_recent_qso;

  uint8_t buf[PORTA_PROTO_MAX_FRAME];
  const size_t n = porta_proto_encode_decode(&ev, buf, sizeof(buf));
  enqueue(buf, n);
}

uint32_t porta_dropped_events() { return s_dropped; }

PortaCompanion porta_companion_state() {
  if (!s_running || s_last_byte_ms == 0) return PortaCompanion::kAbsent;
  const uint32_t now = now_ms();
  if (now - s_last_byte_ms > kCompanionQuietMs) return PortaCompanion::kAbsent;
  // Something is talking and we cannot read it. That is the flag-day case: a
  // sidekick on the previous wire format, which cannot tell us so itself.
  if (s_last_frame_ms == 0 || now - s_last_frame_ms > kCompanionQuietMs) {
    return PortaCompanion::kUnintelligible;
  }
  return s_version_matches ? PortaCompanion::kCurrent : PortaCompanion::kOutOfDate;
}

void porta_start() {
  if (s_running) return;

  gpio_reset_pin(kPortaTxPin);
  gpio_reset_pin(kPortaRxPin);

  // A TX ring buffer is not optional now that this port transmits. With a
  // tx_buffer_size of 0, uart_write_bytes() blocks until every byte has left
  // the FIFO -- about 22 ms for a full frame at 115200, inside the slot loop.
  esp_err_t err = uart_driver_install(kPortaUart, 2048, kTxBufBytes, 0, nullptr, 0);
  if (err == ESP_ERR_INVALID_STATE) {
    uart_driver_delete(kPortaUart);
    err = uart_driver_install(kPortaUart, 2048, kTxBufBytes, 0, nullptr, 0);
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

  porta_decoder_init(&s_dec);
  s_version[0] = '\0';
  s_running = true;
  ESP_LOGI(kTag, "Started on UART%d TX=G%d RX=G%d baud=%d",
           (int)kPortaUart, (int)kPortaTxPin, (int)kPortaRxPin, kBaud);
}

void porta_stop() {
  if (!s_running) return;
  uart_driver_delete(kPortaUart);
  porta_decoder_init(&s_dec);
  s_running = false;
  ESP_LOGI(kTag, "Stopped");
}

void porta_tick() {
  if (!s_running) return;

  uint8_t buf[128];
  const int len = uart_read_bytes(kPortaUart, buf, sizeof(buf), 0);
  porta_frame_t frame;
  if (len > 0) {
    s_last_byte_ms = now_ms();
  }
  for (int i = 0; i < len; ++i) {
    if (porta_decoder_push(&s_dec, buf[i], &frame)) {
      s_last_frame_ms = now_ms();
      handle_frame(frame);
    }
  }

  drain_out();

  // Report drops when the count moves. Without this the queue silently eats
  // events and the browser's view just looks thinner than the screen's, with
  // nothing to say why -- and the drop count is the only evidence that the
  // per-tick send rate is too slow for the traffic.
  static uint32_t s_reported_drops = 0;
  if (s_dropped != s_reported_drops) {
    s_reported_drops = s_dropped;
    ESP_LOGW(kTag, "Dropped %" PRIu32 " outbound events (queue full)", s_dropped);
  }

  // Link health. A wire quietly failing a share of its frames looks identical
  // to a quiet one from up here, so say so when the count moves.
  static uint32_t s_reported_crc = 0;
  if (s_dec.crc_errors != s_reported_crc) {
    s_reported_crc = s_dec.crc_errors;
    ESP_LOGW(kTag, "Link: %" PRIu32 " CRC errors, %" PRIu32 " frames ok",
             s_dec.crc_errors, s_dec.frames_ok);
  }
}
