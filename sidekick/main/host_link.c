#include "host_link.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pairing_http.h"
#include "pairing_http_cap.h"
#include "porta_proto.h"
#include "web_page.h"

#define PORTA_UART UART_NUM_1

static const char *TAG = "host";

// Copy a frame onto the Port A UART without blocking the httpd task forever.
// If the ADV is unplugged or not draining RX, uart_write_bytes() can stall
// indefinitely once the TX ring fills — that hung GET /api/config with no body.
static bool porta_send(const uint8_t *frame, size_t n)
{
    if (!frame || n == 0) {
        return false;
    }
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(300);
    size_t sent = 0;
    while (sent < n) {
        size_t free_sz = 0;
        if (uart_get_tx_buffer_free_size(PORTA_UART, &free_sz) != ESP_OK) {
            ESP_LOGW(TAG, "porta TX: free-size query failed");
            return false;
        }
        if (free_sz == 0) {
            if (xTaskGetTickCount() >= deadline) {
                ESP_LOGW(TAG, "porta TX: ring full (host not draining?)");
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        size_t chunk = n - sent;
        if (chunk > free_sz) {
            chunk = free_sz;
        }
        const int w = uart_write_bytes(PORTA_UART, (const char *)(frame + sent), chunk);
        if (w <= 0) {
            ESP_LOGW(TAG, "porta TX: write failed");
            return false;
        }
        sent += (size_t)w;
    }
    return true;
}

// Ring of recent events. 64 is a couple of FT8 slots' worth of decodes plus
// their log lines -- enough that a browser polling once a second never misses
// anything, and small enough to sit in RAM without thought (~6 KB).
#define RING_LEN 64

typedef struct {
    uint32_t seq;
    uint32_t epoch_secs;   // host's clock at the moment of the event, 0 if unset
    bool     is_decode;
    char     text[PORTA_EVENT_TEXT_MAX + 1];
    int8_t   snr;
    uint16_t offset_hz;
    int16_t  dt_centis;
    bool     is_cq;
    bool     is_to_me;
} entry_t;

static entry_t s_ring[RING_LEN];
static uint32_t s_next_seq = 1;     // 0 is "before anything", so a new client gets all
static SemaphoreHandle_t s_lock;

// The receive task writes and an httpd handler reads, on different tasks. The
// critical sections are a memcpy each, so a plain mutex is the right weight.
static void ring_push(const entry_t *e)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    entry_t *slot = &s_ring[s_next_seq % RING_LEN];
    *slot = *e;
    slot->seq = s_next_seq++;
    xSemaphoreGive(s_lock);
}

// JSON string escaping. Decoded FT8 text is normally plain ASCII, but it comes
// off a radio: a corrupt decode can contain anything, and one stray quote or
// backslash would turn the whole response into a parse error in the browser
// rather than one odd-looking row.
static void json_escape(const char *in, char *out, size_t out_cap)
{
    size_t w = 0;
    for (size_t i = 0; in[i] && w + 7 < out_cap; ++i) {
        const unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            out[w++] = '\\';
            out[w++] = (char)c;
        } else if (c < 0x20 || c == 0x7F) {
            w += (size_t)snprintf(&out[w], out_cap - w, "\\u%04x", c);
        } else {
            out[w++] = (char)c;
        }
    }
    out[w] = '\0';
}

// GET /api/events?since=<seq>
//
// Polling rather than server-sent events: ESP-IDF's httpd dispatches handlers
// on a single task, so a long-lived streaming response would block every other
// request, the status page included. A poll cannot wedge the server, costs a
// second of latency on a 15-second cadence, and matches the polled PORTA design
// so the whole chain works the same way.
static esp_err_t get_events(httpd_req_t *req)
{
    uint32_t since = 0;
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[24];
        if (httpd_query_key_value(query, "since", val, sizeof(val)) == ESP_OK) {
            since = (uint32_t)strtoul(val, NULL, 10);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"events\":[");

    bool first = true;
    char esc[PORTA_EVENT_TEXT_MAX * 6 + 1];
    char row[PORTA_EVENT_TEXT_MAX * 6 + 160];
    uint32_t head;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    head = s_next_seq;
    // Oldest still in the ring. A client that has been away longer than the
    // ring simply resumes from here -- gaps are visible as a jump in seq rather
    // than silently filled with stale rows.
    uint32_t from = (head > RING_LEN) ? (head - RING_LEN) : 0;
    if (since > from) {
        from = since;
    }
    for (uint32_t s = from; s < head; ++s) {
        entry_t e = s_ring[s % RING_LEN];
        if (e.seq != s) {
            continue;   // overwritten while we walked; skip rather than lie
        }
        xSemaphoreGive(s_lock);

        json_escape(e.text, esc, sizeof(esc));
        int n;
        if (e.is_decode) {
            n = snprintf(row, sizeof(row),
                         "%s{\"s\":%" PRIu32 ",\"t\":%" PRIu32 ",\"d\":1,\"x\":\"%s\","
                         "\"snr\":%d,\"hz\":%u,\"dt\":%.2f,\"cq\":%d,\"me\":%d}",
                         first ? "" : ",", e.seq, e.epoch_secs, esc, e.snr, e.offset_hz,
                         e.dt_centis / 100.0, e.is_cq ? 1 : 0, e.is_to_me ? 1 : 0);
        } else {
            n = snprintf(row, sizeof(row),
                         "%s{\"s\":%" PRIu32 ",\"t\":%" PRIu32 ",\"d\":0,\"x\":\"%s\"}",
                         first ? "" : ",", e.seq, e.epoch_secs, esc);
        }
        if (n > 0) {
            httpd_resp_sendstr_chunk(req, row);
            first = false;
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    xSemaphoreGive(s_lock);

    char tail[48];
    snprintf(tail, sizeof(tail), "],\"seq\":%" PRIu32 "}", head);
    httpd_resp_sendstr_chunk(req, tail);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// Last reply the host sent to an action, so the browser learns whether its
// clock actually landed. One outstanding action at a time is all the polled
// design allows, so a single slot is enough and a verb is enough to match it.
// CONFIG_GET/SET reuse the same slot with verb = PORTA_MSG_CONFIG_*.
static volatile uint8_t s_last_reply_verb;
static volatile bool s_last_reply_ok;
static char s_last_reply_reason[PORTA_EVENT_TEXT_MAX + 1];

// CONFIG_GET all: VALUE frames arrive before the ACK; gather into JSON here.
#define CONFIG_JSON_MAX 3072
static char s_config_json[CONFIG_JSON_MAX];
static size_t s_config_json_len;
static bool s_config_json_first;
static volatile bool s_config_gathering;

static void config_json_begin(void)
{
    s_config_gathering = true;
    s_config_json_first = true;
    s_config_json[0] = '{';
    s_config_json[1] = '\0';
    s_config_json_len = 1;
}

static void config_json_add(const char *key, const char *value)
{
    if (!s_config_gathering || !key || !value) {
        return;
    }
    // Static scratch: this runs on the porta_rx task (4 KB stack). Escaping
    // both key and value on the stack overflowed when the host replied.
    static char esc_k[PORTA_CONFIG_KEY_MAX * 6 + 1];
    static char esc_v[PORTA_CONFIG_VALUE_MAX * 6 + 1];
    static char piece[sizeof(esc_k) + sizeof(esc_v) + 8];
    json_escape(key, esc_k, sizeof(esc_k));
    json_escape(value, esc_v, sizeof(esc_v));
    const int n = snprintf(piece, sizeof(piece), "%s\"%s\":\"%s\"",
                           s_config_json_first ? "" : ",", esc_k, esc_v);
    if (n <= 0 || s_config_json_len + (size_t)n + 2 >= CONFIG_JSON_MAX) {
        return;
    }
    memcpy(s_config_json + s_config_json_len, piece, (size_t)n + 1);
    s_config_json_len += (size_t)n;
    s_config_json_first = false;
}

static void config_json_end(void)
{
    if (!s_config_gathering) {
        return;
    }
    if (s_config_json_len + 2 < CONFIG_JSON_MAX) {
        s_config_json[s_config_json_len++] = '}';
        s_config_json[s_config_json_len] = '\0';
    }
    s_config_gathering = false;
}

// Wait briefly for the host's ACK/NAK. One frame over a 115200 link answered
// from the main loop, so this is milliseconds -- but the browser should be
// told what happened rather than assuming success.
static esp_err_t wait_action_reply(httpd_req_t *req, uint8_t verb)
{
    // CONFIG_GET-all can enqueue many VALUE frames (one per tick on the host),
    // so allow longer than a single ACTION. CONNECT may wait on USB enum.
    int tries = 40;
    if (verb == PORTA_MSG_CONFIG_GET) {
        tries = 200;
    } else if (verb == PORTA_ACT_CONNECT) {
        tries = 120;
    }
    for (int i = 0; i < tries && s_last_reply_verb != verb; ++i) {
        vTaskDelay(pdMS_TO_TICKS(25));
    }

    httpd_resp_set_type(req, "application/json");
    if (s_last_reply_verb != verb) {
        s_config_gathering = false;
        ESP_LOGW(TAG, "no ACK for verb 0x%02x", verb);
        return httpd_resp_sendstr(req, "{\"ok\":false,\"why\":\"no reply from host\"}");
    }
    if (s_last_reply_ok) {
        if (verb == PORTA_MSG_CONFIG_GET) {
            return httpd_resp_send(req, s_config_json, s_config_json_len);
        }
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    }
    char out[PORTA_EVENT_TEXT_MAX + 32];
    snprintf(out, sizeof(out), "{\"ok\":false,\"why\":\"%s\"}", s_last_reply_reason);
    return httpd_resp_sendstr(req, out);
}

// POST /api/time  body: epoch_ms
//
// The browser is the clock source in a headless build -- it is the only device
// present that knows the time without internet, which is the case that matters:
// a cold radio on a summit will not decode until UTC is right, and NTP is
// unavailable exactly there. The host decides whether to act; if it is already
// correct it says so and changes nothing.
static esp_err_t post_time(httpd_req_t *req)
{
    char body[48] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        return ESP_FAIL;
    }

    const long long epoch_ms = strtoll(body, NULL, 10);
    if (epoch_ms <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad time");
        return ESP_FAIL;
    }

    uint8_t frame[PORTA_PROTO_MAX_FRAME];
    const size_t n = porta_proto_encode_set_clock((uint32_t)(epoch_ms / 1000),
                                                  (uint16_t)(epoch_ms % 1000),
                                                  frame, sizeof(frame));
    s_last_reply_verb = 0;
    if (!porta_send(frame, n)) {
        return httpd_resp_sendstr(req, "{\"ok\":false,\"why\":\"host link busy\"}");
    }
    return wait_action_reply(req, PORTA_ACT_SET_CLOCK);
}

// POST /api/tx  body: plain free-text (I28a Done-when). Same one-shot path as
// MENU "Send FreeText". Token required: this keys the transmitter.
static esp_err_t post_tx(httpd_req_t *req)
{
    char body[PORTA_EVENT_TEXT_MAX + 1] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        return ESP_FAIL;
    }
    while (received > 0) {
        const char c = body[received - 1];
        if (c != '\n' && c != '\r' && c != ' ' && c != '\t') {
            break;
        }
        body[--received] = '\0';
    }
    if (body[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
        return ESP_FAIL;
    }

    uint8_t frame[PORTA_PROTO_MAX_FRAME];
    const size_t n = porta_proto_encode_tx_free(body, frame, sizeof(frame));
    if (n == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
        return ESP_FAIL;
    }
    s_last_reply_verb = 0;
    if (!porta_send(frame, n)) {
        return httpd_resp_sendstr(req, "{\"ok\":false,\"why\":\"host link busy\"}");
    }
    return wait_action_reply(req, PORTA_ACT_TX_FREE);
}

// POST /api/tx/cancel — abort in-flight TX / clear an armed pending TX.
static esp_err_t post_tx_cancel(httpd_req_t *req)
{
    // Drain any body so a client that POSTs with one does not leave bytes on
    // the socket for the next request on this keep-alive connection.
    char discard[64];
    while (httpd_req_recv(req, discard, sizeof(discard)) > 0) {
    }

    uint8_t frame[PORTA_PROTO_MAX_FRAME];
    const size_t n = porta_proto_encode_tx_cancel(frame, sizeof(frame));
    s_last_reply_verb = 0;
    if (!porta_send(frame, n)) {
        return httpd_resp_sendstr(req, "{\"ok\":false,\"why\":\"host link busy\"}");
    }
    return wait_action_reply(req, PORTA_ACT_TX_CANCEL);
}

// POST /api/radio/connect — STATUS → 2 (start UAC + CAT sync). Token required.
static esp_err_t post_radio_connect(httpd_req_t *req)
{
    char discard[64];
    while (httpd_req_recv(req, discard, sizeof(discard)) > 0) {
    }

    uint8_t frame[PORTA_PROTO_MAX_FRAME];
    const size_t n = porta_proto_encode_connect(frame, sizeof(frame));
    s_last_reply_verb = 0;
    if (!porta_send(frame, n)) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"why\":\"host link busy\"}");
    }
    return wait_action_reply(req, PORTA_ACT_CONNECT);
}

// POST /api/radio/tune — body "1"/"0" or "on"/"off". Token required (keys TX).
static esp_err_t post_radio_tune(httpd_req_t *req)
{
    char body[16] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        return ESP_FAIL;
    }
    while (received > 0) {
        const char c = body[received - 1];
        if (c != '\n' && c != '\r' && c != ' ' && c != '\t') {
            break;
        }
        body[--received] = '\0';
    }
    bool on = false;
    if (strcmp(body, "1") == 0 || strcmp(body, "on") == 0 || strcmp(body, "true") == 0) {
        on = true;
    } else if (strcmp(body, "0") == 0 || strcmp(body, "off") == 0 ||
               strcmp(body, "false") == 0) {
        on = false;
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "want 0 or 1");
        return ESP_FAIL;
    }

    uint8_t frame[PORTA_PROTO_MAX_FRAME];
    const size_t n = porta_proto_encode_tune(on, frame, sizeof(frame));
    s_last_reply_verb = 0;
    if (!porta_send(frame, n)) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"why\":\"host link busy\"}");
    }
    return wait_action_reply(req, PORTA_ACT_TUNE);
}

// GET /api/config — Station.txt surface as JSON. Open read: call/grid are on
// the air; WiFi secrets are not in this object.
static esp_err_t get_config(httpd_req_t *req)
{
    config_json_begin();
    s_last_reply_verb = 0;
    uint8_t frame[PORTA_PROTO_MAX_FRAME];
    const size_t n = porta_proto_encode_config_get("", frame, sizeof(frame));
    if (!porta_send(frame, n)) {
        s_config_gathering = false;
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"why\":\"host link busy\"}");
    }
    return wait_action_reply(req, PORTA_MSG_CONFIG_GET);
}

// PUT /api/config  body: one or more Station.txt lines (key=value\n).
// Token required: this changes the station.
static esp_err_t put_config(httpd_req_t *req)
{
    char body[1024];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    int applied = 0;
    char *line = body;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = '\0';
        }
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) {
            line[--len] = '\0';
        }
        if (len > 0) {
            char *eq = strchr(line, '=');
            if (!eq || eq == line) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad line");
                return ESP_FAIL;
            }
            *eq = '\0';
            const char *key = line;
            const char *value = eq + 1;
            uint8_t frame[PORTA_PROTO_MAX_FRAME];
            const size_t n = porta_proto_encode_config_set(key, value, frame, sizeof(frame));
            if (n == 0) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad key/value");
                return ESP_FAIL;
            }
            s_last_reply_verb = 0;
            if (!porta_send(frame, n)) {
                httpd_resp_set_type(req, "application/json");
                return httpd_resp_sendstr(req, "{\"ok\":false,\"why\":\"host link busy\"}");
            }
            for (int i = 0; i < 40 && s_last_reply_verb != PORTA_MSG_CONFIG_SET; ++i) {
                vTaskDelay(pdMS_TO_TICKS(25));
            }
            if (s_last_reply_verb != PORTA_MSG_CONFIG_SET) {
                httpd_resp_set_type(req, "application/json");
                return httpd_resp_sendstr(req, "{\"ok\":false,\"why\":\"no reply from host\"}");
            }
            if (!s_last_reply_ok) {
                char out[PORTA_EVENT_TEXT_MAX + 32];
                snprintf(out, sizeof(out), "{\"ok\":false,\"why\":\"%s\"}", s_last_reply_reason);
                httpd_resp_set_type(req, "application/json");
                return httpd_resp_sendstr(req, out);
            }
            ++applied;
        }
        line = nl ? nl + 1 : NULL;
    }
    if (applied == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t get_viewer(httpd_req_t *req)
{
    // No substitutions -- the viewer fetches everything it shows from
    // /api/events. Sent through web_page_send_file() so a placeholder added
    // to the file later is expanded rather than rendered as literal braces.
    return web_page_send_file(req, "viewer.html", NULL, 0);
}

// Its own task rather than a poll in the beacon loop: events arrive
// continuously, and a second of latency would make a live viewer useless quite
// apart from overrunning the RX buffer.
static void porta_rx_task(void *arg)
{
    (void)arg;
    porta_decoder_t dec;
    porta_decoder_init(&dec);

    uint8_t buf[256];
    porta_frame_t frame;
    char text[PORTA_EVENT_TEXT_MAX + 1];
    porta_decode_event_t ev;
    uint32_t reported_crc = 0;

    while (1) {
        const int n = uart_read_bytes(PORTA_UART, buf, sizeof(buf), pdMS_TO_TICKS(50));
        for (int i = 0; i < n; ++i) {
            if (!porta_decoder_push(&dec, buf[i], &frame)) {
                continue;
            }
            entry_t e = {0};
            if (porta_proto_parse_log(&frame, &e.epoch_secs, text)) {
                strncpy(e.text, text, sizeof(e.text) - 1);
                // Tag "adv" so USB-C logs are not mistaken for sidekick ESP_LOG.
                ESP_LOGI("adv", "%s", e.text);
                ring_push(&e);
            } else if (porta_proto_parse_decode(&frame, &ev)) {
                e.is_decode = true;
                e.epoch_secs = ev.epoch_secs;
                strncpy(e.text, ev.text, sizeof(e.text) - 1);
                e.snr = ev.snr;
                e.offset_hz = ev.offset_hz;
                e.dt_centis = ev.dt_centis;
                e.is_cq = ev.is_cq;
                e.is_to_me = ev.is_to_me;
                ESP_LOGI("adv", "decode %+d dB %4u Hz %+.2f s %s%s%s",
                         e.snr, e.offset_hz, e.dt_centis / 100.0,
                         e.is_to_me ? "[me] " : "", e.is_cq ? "[cq] " : "", e.text);
                ring_push(&e);
            } else {
                char cfg_key[PORTA_CONFIG_KEY_MAX + 1];
                char cfg_val[PORTA_CONFIG_VALUE_MAX + 1];
                if (porta_proto_parse_config_value(&frame, cfg_key, cfg_val)) {
                    config_json_add(cfg_key, cfg_val);
                } else if (porta_proto_parse_ack(&frame, (uint8_t *)&s_last_reply_verb)) {
                    if (s_last_reply_verb == PORTA_MSG_CONFIG_GET) {
                        config_json_end();
                    }
                    s_last_reply_ok = true;
                    s_last_reply_reason[0] = '\0';
                } else if (porta_proto_parse_nak(&frame, (uint8_t *)&s_last_reply_verb,
                                                 s_last_reply_reason)) {
                    s_config_gathering = false;
                    s_last_reply_ok = false;
                    ESP_LOGW("adv", "refused action 0x%02x: %s",
                             s_last_reply_verb, s_last_reply_reason);
                } else {
                    ESP_LOGW(TAG, "frame type 0x%02x len %u (no handler yet)",
                             frame.type, frame.len);
                }
            }
        }
        if (dec.crc_errors != reported_crc) {
            reported_crc = dec.crc_errors;
            ESP_LOGW(TAG, "link: %" PRIu32 " CRC errors, %" PRIu32 " frames ok",
                     dec.crc_errors, dec.frames_ok);
        }
    }
}

void host_link_start(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    xTaskCreate(porta_rx_task, "porta_rx", 4096, NULL, 5, NULL);
}

void host_link_register_uris(httpd_handle_t server)
{
    if (!server) {
        return;
    }
    static const httpd_uri_t viewer = {
        .uri = "/log", .method = HTTP_GET, .handler = get_viewer,
    };
    static const httpd_uri_t events = {
        .uri = "/api/events", .method = HTTP_GET, .handler = get_events,
    };
    static const httpd_uri_t settime = {
        .uri = "/api/time", .method = HTTP_POST, .handler = post_time,
    };
    static const httpd_uri_t tx = {
        .uri = "/api/tx", .method = HTTP_POST, .handler = post_tx,
    };
    static const httpd_uri_t tx_cancel = {
        .uri = "/api/tx/cancel", .method = HTTP_POST, .handler = post_tx_cancel,
    };
    static const httpd_uri_t config_get = {
        .uri = "/api/config", .method = HTTP_GET, .handler = get_config,
    };
    static const httpd_uri_t config_put = {
        .uri = "/api/config", .method = HTTP_PUT, .handler = put_config,
    };
    static const httpd_uri_t radio_connect = {
        .uri = "/api/radio/connect", .method = HTTP_POST, .handler = post_radio_connect,
    };
    static const httpd_uri_t radio_tune = {
        .uri = "/api/radio/tune", .method = HTTP_POST, .handler = post_radio_tune,
    };
    // The viewer and the event feed are reads of radio data: open on
    // principle, since anyone may listen to what is on the air. Setting the
    // host clock or keying the transmitter changes the device, so those need
    // the token. Station config read is open (call/grid are on the air);
    // writes are guarded. Connect / tune are writes (UAC + CAT / TX tone).
    const struct {
        const httpd_uri_t *uri;
        pairing_policy_t policy;
    } routes[] = {
        { &viewer, PAIRING_OPEN },
        { &events, PAIRING_OPEN },
        { &settime, PAIRING_REQUIRED },
        { &tx, PAIRING_REQUIRED },
        { &tx_cancel, PAIRING_REQUIRED },
        { &config_get, PAIRING_OPEN },
        { &config_put, PAIRING_REQUIRED },
        { &radio_connect, PAIRING_REQUIRED },
        { &radio_tune, PAIRING_REQUIRED },
    };
    _Static_assert(sizeof(routes) / sizeof(routes[0]) == PAIRING_ROUTES_HOST_LINK,
                   "host_link route count");
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        const esp_err_t err = pairing_http_register(server, routes[i].uri, routes[i].policy);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed: %s", routes[i].uri->uri, esp_err_to_name(err));
        }
    }
}
