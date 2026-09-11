#include "host_link.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "porta_proto.h"

#define PORTA_UART UART_NUM_1

static const char *TAG = "host";

// Ring of recent events. 64 is a couple of FT8 slots' worth of decodes plus
// their log lines -- enough that a browser polling once a second never misses
// anything, and small enough to sit in RAM without thought (~6 KB).
#define RING_LEN 64

typedef struct {
    uint32_t seq;
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
                         "%s{\"s\":%" PRIu32 ",\"d\":1,\"x\":\"%s\",\"snr\":%d,"
                         "\"hz\":%u,\"dt\":%.2f,\"cq\":%d,\"me\":%d}",
                         first ? "" : ",", e.seq, esc, e.snr, e.offset_hz,
                         e.dt_centis / 100.0, e.is_cq ? 1 : 0, e.is_to_me ? 1 : 0);
        } else {
            n = snprintf(row, sizeof(row), "%s{\"s\":%" PRIu32 ",\"d\":0,\"x\":\"%s\"}",
                         first ? "" : ",", e.seq, esc);
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

static const char kViewerPage[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Mini-FT8 host log</title>"
    "<style>body{font-family:ui-monospace,monospace;margin:0;padding:.5rem;"
    "background:#111;color:#ddd;font-size:13px}"
    "#s{color:#888;font-size:11px;padding:.2rem 0}"
    "div.r{padding:.15rem 0;border-bottom:1px solid #222;white-space:pre-wrap}"
    ".cq{color:#7fd}.me{color:#fd7}.lg{color:#999}</style>"
    "<div id=s>connecting…</div><div id=o></div>"
    "<script>let q=0,o=document.getElementById('o'),s=document.getElementById('s');"
    "async function t(){try{"
    "let r=await fetch('/api/events?since='+q),j=await r.json();"
    "for(const e of j.events){let d=document.createElement('div');d.className='r';"
    "d.textContent=e.d?((e.snr>0?'+':'')+e.snr).padStart(3)+' '+String(e.hz).padStart(4)+'Hz '"
    "+(e.dt>0?'+':'')+e.dt.toFixed(1)+' '+e.x:'· '+e.x;"
    "if(e.d&&e.me)d.classList.add('me');else if(e.d&&e.cq)d.classList.add('cq');"
    "else if(!e.d)d.classList.add('lg');"
    "o.insertBefore(d,o.firstChild);}"
    "q=j.seq;while(o.childNodes.length>200)o.removeChild(o.lastChild);"
    "s.textContent='live · '+q+' events';"
    "}catch(e){s.textContent='disconnected — retrying';}"
    "setTimeout(t,1000);}t();</script>";

static esp_err_t get_viewer(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kViewerPage, HTTPD_RESP_USE_STRLEN);
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
            if (porta_proto_parse_log(&frame, text)) {
                strncpy(e.text, text, sizeof(e.text) - 1);
                ESP_LOGI(TAG, "%s", e.text);
                ring_push(&e);
            } else if (porta_proto_parse_decode(&frame, &ev)) {
                e.is_decode = true;
                strncpy(e.text, ev.text, sizeof(e.text) - 1);
                e.snr = ev.snr;
                e.offset_hz = ev.offset_hz;
                e.dt_centis = ev.dt_centis;
                e.is_cq = ev.is_cq;
                e.is_to_me = ev.is_to_me;
                ESP_LOGI(TAG, "decode %+d dB %4u Hz %+.2f s %s%s%s",
                         e.snr, e.offset_hz, e.dt_centis / 100.0,
                         e.is_to_me ? "[me] " : "", e.is_cq ? "[cq] " : "", e.text);
                ring_push(&e);
            } else {
                ESP_LOGW(TAG, "frame type 0x%02x len %u (no handler yet)",
                         frame.type, frame.len);
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
    httpd_register_uri_handler(server, &viewer);
    httpd_register_uri_handler(server, &events);
}
