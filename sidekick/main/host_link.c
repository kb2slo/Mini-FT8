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
#include "porta_proto.h"

#define PORTA_UART UART_NUM_1

static const char *TAG = "host";

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

// Newest at the bottom, terminal-style, with the standard stick-to-bottom
// mechanic: measure whether the view is already at the bottom *before*
// appending, and only scroll if it was. Scrolling up is therefore a deliberate
// act that is never undone by an arriving row -- which is the whole point, since
// the rows arrive on their own schedule and reading history is when you least
// want the view yanked away.
//
// Two details that matter and are easy to miss. `overflow-anchor: none` stops
// the browser's own scroll anchoring from fighting the same job and producing a
// jitter neither mechanism intends. And trimming old rows happens only while
// following: removing from the top while someone is reading history shifts
// everything under their eyes.
//
// An unset host clock renders as --:--:-- rather than as a 1970 timestamp or a
// blank. FT8 will not decode without a synced clock, so "the host does not know
// what time it is" is diagnostic information, not an absence.
static const char kViewerPage[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Mini-FT8 host log</title>"
    "<style>html,body{height:100%;margin:0}"
    "body{display:flex;flex-direction:column;font-family:ui-monospace,monospace;"
    "background:#111;color:#ddd;font-size:13px}"
    "#s{flex:none;color:#888;font-size:11px;padding:.35rem .5rem;border-bottom:1px solid #222}"
    "#o{flex:1;overflow-y:auto;overflow-anchor:none;padding:.25rem .5rem}"
    "div.r{padding:.1rem 0;white-space:pre-wrap;word-break:break-word}"
    ".ts{color:#555}.cq{color:#7fd}.me{color:#fd7}.lg{color:#999}</style>"
    "<div id=s>connecting\xE2\x80\xA6</div><div id=o></div>"
    "<script>"
    "let q=0,o=document.getElementById('o'),s=document.getElementById('s'),follow=true;"
    "function atBottom(){return o.scrollHeight-o.scrollTop-o.clientHeight<40}"
    "function stat(){s.textContent=follow?('live \xC2\xB7 '+q+' events')"
    ":('paused \xC2\xB7 scroll to the bottom to follow \xC2\xB7 '+q+' events')}"
    "o.addEventListener('scroll',function(){follow=atBottom();stat()});"
    "function hhmmss(t){if(!t)return'--:--:--';"
    "return new Date(t*1000).toISOString().substr(11,8)}"
    "function row(e){var d=document.createElement('div');d.className='r';"
    "var a=document.createElement('span');a.className='ts';a.textContent=hhmmss(e.t)+' ';"
    "var b=document.createElement('span');"
    "if(e.d){b.textContent=((e.snr>0?'+':'')+e.snr).padStart(3)+' '"
    "+String(e.hz).padStart(4)+' '+(e.dt>0?'+':'')+e.dt.toFixed(1)+' '+e.x;"
    "b.className=e.me?'me':(e.cq?'cq':'')}"
    "else{b.textContent='\xC2\xB7 '+e.x;b.className='lg'}"
    "d.appendChild(a);d.appendChild(b);return d}"
    "var synced=false;"
    "async function sync(){if(synced)return;synced=true;"
    // Round-trip halving, the same reasoning NTP uses: the host should be set
    // to the time at the midpoint of the exchange, not the time we started it.
    // Over WiFi this is tens of milliseconds against FT8's one-second
    // tolerance, so it is belt and braces rather than necessity.
    "var t0=Date.now();"
    // Setting the clock is a write, so it carries the pairing token while
    // watching does not. An unpaired browser sees everything and changes
    // nothing, which is the whole of RFC 0004 §7 in one request.
    "try{var r=await fetch('/api/time',{method:'POST',"
    "headers:{'X-MiniFT8-Token':localStorage.getItem('minift8_token')||''},"
    "body:String(t0+Math.round((Date.now()-t0)/2))});"
    "if(r.status==401){s.textContent="
    "'clock not set \xE2\x80\x94 unpaired (button, then /api/pairing-token)';return}"
    "var j=await r.json();"
    "if(!j.ok)s.textContent='clock not set: '+j.why;}"
    "catch(e){synced=false}}"
    "async function tick(){try{"
    "var r=await fetch('/api/events?since='+q),j=await r.json();"
    "if(j.events.length){var was=atBottom();"
    "for(var i=0;i<j.events.length;i++)o.appendChild(row(j.events[i]));"
    "if(was){o.scrollTop=o.scrollHeight;follow=true}"
    "if(follow){while(o.childNodes.length>300)o.removeChild(o.firstChild)}}"
    "q=j.seq;stat();sync()}"
    "catch(e){s.textContent='disconnected \xE2\x80\x94 retrying'}"
    "setTimeout(tick,1000)}tick();</script>";

// Last reply the host sent to an action, so the browser learns whether its
// clock actually landed. One outstanding action at a time is all the polled
// design allows, so a single slot is enough and a verb is enough to match it.
static volatile uint8_t s_last_reply_verb;
static volatile bool s_last_reply_ok;
static char s_last_reply_reason[PORTA_EVENT_TEXT_MAX + 1];

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
    uart_write_bytes(PORTA_UART, (const char *)frame, n);

    // Wait briefly for the host's answer. It is one frame over a 115200 link
    // answered from the main loop, so this is milliseconds -- but the browser
    // should be told what happened rather than assuming success.
    for (int i = 0; i < 40 && s_last_reply_verb != PORTA_ACT_SET_CLOCK; ++i) {
        vTaskDelay(pdMS_TO_TICKS(25));
    }

    httpd_resp_set_type(req, "application/json");
    if (s_last_reply_verb != PORTA_ACT_SET_CLOCK) {
        return httpd_resp_sendstr(req, "{\"ok\":false,\"why\":\"no reply from host\"}");
    }
    if (s_last_reply_ok) {
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    }
    char out[PORTA_EVENT_TEXT_MAX + 32];
    snprintf(out, sizeof(out), "{\"ok\":false,\"why\":\"%s\"}", s_last_reply_reason);
    return httpd_resp_sendstr(req, out);
}

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
            if (porta_proto_parse_log(&frame, &e.epoch_secs, text)) {
                strncpy(e.text, text, sizeof(e.text) - 1);
                ESP_LOGI(TAG, "%s", e.text);
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
                ESP_LOGI(TAG, "decode %+d dB %4u Hz %+.2f s %s%s%s",
                         e.snr, e.offset_hz, e.dt_centis / 100.0,
                         e.is_to_me ? "[me] " : "", e.is_cq ? "[cq] " : "", e.text);
                ring_push(&e);
            } else if (porta_proto_parse_ack(&frame, (uint8_t *)&s_last_reply_verb)) {
                s_last_reply_ok = true;
                s_last_reply_reason[0] = '\0';
            } else if (porta_proto_parse_nak(&frame, (uint8_t *)&s_last_reply_verb,
                                             s_last_reply_reason)) {
                s_last_reply_ok = false;
                ESP_LOGW(TAG, "host refused action 0x%02x: %s",
                         s_last_reply_verb, s_last_reply_reason);
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
    static const httpd_uri_t settime = {
        .uri = "/api/time", .method = HTTP_POST, .handler = post_time,
    };
    // The viewer and the event feed are reads of radio data: open on
    // principle, since anyone may listen to what is on the air. Setting the
    // host clock changes the device, so it needs the token.
    pairing_http_register(server, &viewer, PAIRING_OPEN);
    pairing_http_register(server, &events, PAIRING_OPEN);
    pairing_http_register(server, &settime, PAIRING_REQUIRED);
}
