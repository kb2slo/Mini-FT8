#include "wifi_prov.h"

#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "dns_server.h"
#include "mdns.h"

static const char *TAG = "wifi_prov";

#define NVS_NAMESPACE "sidekick"
#define NVS_KEY_SSID  "wifi_ssid"
#define NVS_KEY_PASS  "wifi_pass"

// WPA2 maxima, plus a NUL. Sized from the spec rather than guessed: an SSID is
// up to 32 bytes and a PSK up to 63 characters.
#define SSID_MAX 32
#define PASS_MAX 64

// Join attempts before giving up and raising the AP. Each retry costs a couple
// of seconds; the point is to fall back promptly rather than leave an operator
// staring at a device that looks dead but is quietly retrying forever.
#define JOIN_MAX_RETRY 5

// Upper bound on the whole join attempt, retries included. This is a backstop,
// not the normal exit: every ordinary outcome sets one of the event bits. It
// exists because the alternative -- waiting forever -- turns any lost or
// unhandled event into a device that sits there with no AP, no station, and
// no way back in. The finished product has no serial console to explain that.
#define JOIN_TIMEOUT_MS (60 * 1000)

static EventGroupHandle_t s_events;
#define BIT_GOT_IP   BIT0
#define BIT_JOIN_FAIL BIT1

static int  s_retries;
static bool s_online;
static bool s_ap_active;
static char s_ssid[SSID_MAX + 1];
static httpd_handle_t s_httpd;

// Created at most once each. esp_netif_create_default_wifi_*() asserts on a
// duplicate if_key rather than returning an error, so calling it twice is a
// panic, not a failure to handle. That is exactly what happened on the
// stored-credentials-failed path: try_join() created the STA netif, the join
// failed, and start_ap() created it again for APSTA scanning.
static esp_netif_t *s_netif_sta;
static esp_netif_t *s_netif_ap;
static dns_server_handle_t s_dns;
static volatile int s_ap_clients;

static void ensure_sta_netif(void)
{
    if (!s_netif_sta) {
        s_netif_sta = esp_netif_create_default_wifi_sta();
    }
}

static void ensure_ap_netif(void)
{
    if (!s_netif_ap) {
        s_netif_ap = esp_netif_create_default_wifi_ap();
    }
}

// ---------------------------------------------------------------------------
// Stored credentials
// ---------------------------------------------------------------------------

static bool creds_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t n = ssid_len;
    bool ok = (nvs_get_str(h, NVS_KEY_SSID, ssid, &n) == ESP_OK) && ssid[0] != '\0';
    if (ok) {
        n = pass_len;
        // A missing password is legitimate: open networks exist.
        if (nvs_get_str(h, NVS_KEY_PASS, pass, &n) != ESP_OK) {
            pass[0] = '\0';
        }
    }
    nvs_close(h);
    return ok;
}

static bool creds_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_str(h, NVS_KEY_SSID, ssid) == ESP_OK &&
              nvs_set_str(h, NVS_KEY_PASS, pass) == ESP_OK &&
              nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

// Clears the stored credentials. With WIFI_STORAGE_RAM there is nothing else
// to clear -- that is the point of setting it.
static bool creds_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    (void)nvs_erase_key(h, NVS_KEY_SSID);
    (void)nvs_erase_key(h, NVS_KEY_PASS);
    const bool ok = nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

// ---------------------------------------------------------------------------
// WiFi events
// ---------------------------------------------------------------------------

static uint8_t s_last_disconnect_reason;

// True only while we are deliberately trying to join. The AP runs in APSTA so
// it can scan, and bringing that up fires WIFI_EVENT_STA_START just like a
// real join does -- without this the fallback AP would auto-connect to the
// network that had just failed, retry it forever, and take the radio off
// channel each time while an operator is trying to use the provisioning page.
static bool s_join_active;

// The handful of reasons an operator can actually act on. Anything else falls
// through to the raw code, which is still better than silence.
static const char *disconnect_reason_text(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
        // On a 2.4 GHz-only part this is very often a 5 GHz network. An
        // iPhone Personal Hotspot defaults to 5 GHz on recent handsets and
        // needs "Maximize Compatibility" turned on to be visible at all.
        return "network not found (5 GHz? this radio is 2.4 GHz only)";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "wrong password";
    case WIFI_REASON_AUTH_EXPIRE:
        return "auth expired";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "signal lost";
    default:
        return "see reason code";
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_join_active) {
            esp_wifi_connect();
        }
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d =
            (const wifi_event_sta_disconnected_t *)data;
        s_online = false;
        s_last_disconnect_reason = d ? d->reason : 0;
        if (!s_join_active) {
            return;  // APSTA station side idling alongside the AP; not a join
        }
        // The reason code is the difference between "wrong password" and
        // "that network is not reachable from here", which look identical
        // from the operator's side and have completely different fixes.
        if (s_retries < JOIN_MAX_RETRY) {
            s_retries++;
            ESP_LOGW(TAG, "Join failed (%s), retry %d/%d",
                     disconnect_reason_text(s_last_disconnect_reason),
                     s_retries, JOIN_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Join failed after %d tries: %s", JOIN_MAX_RETRY,
                     disconnect_reason_text(s_last_disconnect_reason));
            xEventGroupSetBits(s_events, BIT_JOIN_FAIL);
        }
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        s_ap_clients++;
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (s_ap_clients > 0) {
            s_ap_clients--;
        }
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Online as " IPSTR " on '%s'", IP2STR(&e->ip_info.ip), s_ssid);
        s_retries = 0;
        s_online = true;
        xEventGroupSetBits(s_events, BIT_GOT_IP);
    }
}

// ---------------------------------------------------------------------------
// Provisioning page
// ---------------------------------------------------------------------------

// Declare the encoding rather than letting the browser guess. Without it a
// browser falls back to Latin-1 and every multi-byte character arrives as
// mojibake -- the ellipsis in the "Forgotten" page rendered as "a<TM>|".
// Our own em dashes are the visible symptom; the case that matters is that
// SSIDs are not ASCII-only, and the scan list renders whatever the neighbours
// have named their networks straight into the dropdown.
#define HTML_UTF8 "text/html; charset=utf-8"

// Scan results are rendered into the form rather than fetched by script: no
// JS means no second request, no JSON parser on the device, and it still works
// in whatever browser a phone opens a captive portal in.
//
// The scan is taken once when the AP comes up and then cached, NOT run per
// request. Scanning in APSTA mode takes the radio off the AP's channel for a
// few hundred ms per channel, which can drop a connected phone — and doing
// that while serving the very page the phone asked for would look like "the
// page doesn't load", which is close to undiagnosable from the operator's
// side. /rescan exists for when the list is genuinely stale, and accepts the
// hiccup deliberately.
#define SCAN_MAX_APS 20
#define SCAN_BUF_LEN 2560

static char s_scan_options[SCAN_BUF_LEN];
static int  s_scan_count;

// HTML-escape into `out`. An SSID is arbitrary bytes chosen by someone else;
// dropping it into a page unescaped is how a neighbour's network name becomes
// script running on the operator's phone.
static void html_escape(const char *in, char *out, size_t out_len)
{
    size_t w = 0;
    for (const char *r = in; *r && w + 7 < out_len; ++r) {
        const char *rep = NULL;
        switch (*r) {
        case '&':  rep = "&amp;";  break;
        case '<':  rep = "&lt;";   break;
        case '>':  rep = "&gt;";   break;
        case '"':  rep = "&quot;"; break;
        case '\'': rep = "&#39;";  break;
        default: break;
        }
        if (rep) {
            size_t n = strlen(rep);
            memcpy(out + w, rep, n);
            w += n;
        } else {
            out[w++] = *r;
        }
    }
    out[w] = '\0';
}

// Renders <option> rows for the networks in range, strongest first (the
// driver already sorts by RSSI). Returns the number found; 0 means the
// caller should fall back to a plain text field rather than an empty list.
static int render_scan_options(char *out, size_t out_len)
{
    wifi_scan_config_t scan = {.show_hidden = false};
    if (esp_wifi_scan_start(&scan, true) != ESP_OK) {
        ESP_LOGW(TAG, "Scan failed");
        return 0;
    }
    uint16_t found = SCAN_MAX_APS;
    static wifi_ap_record_t records[SCAN_MAX_APS];
    if (esp_wifi_scan_get_ap_records(&found, records) != ESP_OK) {
        return 0;
    }

    size_t w = 0;
    int shown = 0;
    for (uint16_t i = 0; i < found; ++i) {
        const char *ssid = (const char *)records[i].ssid;
        if (ssid[0] == '\0') {
            continue;  // hidden network: nothing useful to show or select
        }
        // Skip duplicates — the same SSID on 2.4 and 5 GHz, or a mesh with
        // several APs, otherwise fills the list with the same name repeatedly.
        bool dup = false;
        for (uint16_t j = 0; j < i; ++j) {
            if (strcmp(ssid, (const char *)records[j].ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        char esc[SSID_MAX * 6 + 1];
        html_escape(ssid, esc, sizeof(esc));
        int n = snprintf(out + w, out_len - w,
                         "<option value=\"%s\">%s%s</option>",
                         esc, esc, records[i].authmode == WIFI_AUTH_OPEN ? " (open)" : "");
        if (n < 0 || (size_t)n >= out_len - w) {
            break;  // page buffer full; show what fitted
        }
        w += n;
        shown++;
    }
    ESP_LOGI(TAG, "Scan: %u found, %d listed", found, shown);
    return shown;
}

// Dropdown value meaning "not in the list". A real network could in principle
// be named this; the consequence is benign, since picking Other and typing the
// same string still joins it.
#define SSID_OTHER "__other__"

// The manual field is revealed rather than always shown: it was previously a
// second always-visible box next to the dropdown, which made the form ask two
// questions where the operator only has one answer. The <noscript> block keeps
// it reachable if the captive-portal webview has scripting off -- without that
// fallback, a browser with no JS could never enter a hidden network.
static const char kFormOtherBox[] =
    "<div id=oth hidden>"
    "<label style='font-size:.85rem'>Network name</label>"
    "<input id=othname name=ssid_manual maxlength=32></div>"
    "<noscript><style>#oth{display:block!important}</style></noscript>";

static const char kFormPage[] =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Mini-FT8 sidekick</title>"
    "<style>body{font-family:system-ui;margin:2rem auto;max-width:22rem;padding:0 1rem}"
    "input,select{width:100%;padding:.6rem;margin:.3rem 0 1rem;font-size:1rem}"
    "button{width:100%;padding:.7rem;font-size:1rem}</style>"
    "<h2>Mini-FT8 sidekick</h2>"
    "<p>Join this device to your WiFi. <b>2.4 GHz only</b> — this radio cannot see "
    "5 GHz networks. An iPhone Personal Hotspot needs <i>Maximize Compatibility</i> "
    "switched on before it will appear.</p>"
    "<form method=POST action=/provision>";

// Tail of the page, after the scan results are spliced in. The manual field
// stays: a hidden SSID will not appear in a scan, and a network can be out of
// range at provisioning time but present later.
static const char kFormTail[] =
    "<label>Password</label><input name=pass type=password maxlength=63>"
    "<button>Save and join</button></form>"
    "<p style='font-size:.85rem;color:#666'><a href='/rescan'>Scan again</a> "
    "if your network is missing. This may briefly drop your phone's connection "
    "to the sidekick.</p>";

// Our own error response instead of httpd_resp_send_err(), which hardcodes
// Content-Type: text/html with no charset -- the same Latin-1 guess that
// mangled the pages above -- and is not overridable, because it sets the type
// itself after the handler has run. Error strings are the easiest place for
// this to go unnoticed: they are written once, seen rarely, and are exactly
// where a non-ASCII character ends up when the text explains something.
//
// Returns ESP_FAIL so the caller keeps the connection-closing behaviour it had
// before. That matters for the "too long" path, which rejects a request whose
// body it has not drained; leaving the socket open would have the remainder
// parsed as the next request.
static esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, HTML_UTF8);
    char page[512];
    snprintf(page, sizeof(page),
             "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
             "<title>Mini-FT8 sidekick</title>"
             "<body style='font-family:system-ui;margin:2rem auto;max-width:22rem;padding:0 1rem'>"
             "<h2>Not saved</h2><p>%s</p><p><a href='/'>Back to the form</a></p>", msg);
    httpd_resp_sendstr(req, page);
    return ESP_FAIL;
}

static const char kManualOnly[] =
    "<label>Network</label><input name=ssid maxlength=32 required autofocus>";

// Minimal application/x-www-form-urlencoded field extractor. httpd_query_key_value
// works on this encoding and handles the key matching; the only thing it leaves
// is percent- and plus-decoding, which a WiFi password very much needs.
static void url_decode(char *s)
{
    char *w = s;
    for (char *r = s; *r; ++r) {
        if (*r == '+') {
            *w++ = ' ';
        } else if (*r == '%' && r[1] && r[2]) {
            char hex[3] = {r[1], r[2], 0};
            *w++ = (char)strtol(hex, NULL, 16);
            r += 2;
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

// Refresh the cached list. Safe to call with a client connected, but it may
// briefly interrupt them — see the note above.
static void scan_refresh(void)
{
    s_scan_count = render_scan_options(s_scan_options, sizeof(s_scan_options));
}

// The cache goes stale in the obvious way: the AP comes up, then the operator
// turns on their phone hotspot, and the list they see predates it. Field-hit
// on the first bench run. Rescanning while a client is connected risks
// dropping them (APSTA scan takes the radio off channel), so this refreshes
// only while nobody is associated -- which is exactly the window before
// someone opens the page.
static void scan_keepfresh_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (!s_ap_active || s_ap_clients > 0) {
            continue;
        }
        scan_refresh();
    }
}

static esp_err_t get_rescan(httpd_req_t *req)
{
    scan_refresh();
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t get_form(httpd_req_t *req)
{
    const int found = s_scan_count;
    const char *options = s_scan_options;

    httpd_resp_set_type(req, HTML_UTF8);
    httpd_resp_sendstr_chunk(req, kFormPage);
    if (s_last_disconnect_reason != 0) {
        char why[160];
        snprintf(why, sizeof(why),
                 "<p style='background:#fee;padding:.6rem;border-radius:.3rem'>"
                 "Last attempt failed: %s.</p>",
                 disconnect_reason_text(s_last_disconnect_reason));
        httpd_resp_sendstr_chunk(req, why);
    }
    if (found > 0) {
        httpd_resp_sendstr_chunk(req,
            "<label>Network</label><select name=ssid required autofocus "
            "onchange=\"var b=document.getElementById('oth');"
            "b.hidden=this.value!='" SSID_OTHER "';"
            "if(!b.hidden)document.getElementById('othname').focus()\">");
        httpd_resp_sendstr_chunk(req, options);
        // Escape hatch for hidden or out-of-range networks, last in the list
        // because it is the exception.
        httpd_resp_sendstr_chunk(req,
            "<option value=\"" SSID_OTHER "\">Other…</option></select>");
        httpd_resp_sendstr_chunk(req, kFormOtherBox);
    } else {
        httpd_resp_sendstr_chunk(req, kManualOnly);
    }
    httpd_resp_sendstr_chunk(req, kFormTail);
    return httpd_resp_sendstr_chunk(req, NULL);  // end of response
}

static esp_err_t post_provision(httpd_req_t *req)
{
    char body[SSID_MAX + PASS_MAX + 32];
    int received = 0;
    if (req->content_len >= (int)sizeof(body)) {
        return send_error(req, "400 Bad Request", "That form submission was too large.");
    }
    while (received < req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            return ESP_FAIL;
        }
        received += r;
    }
    body[received] = '\0';

    char ssid[SSID_MAX + 1] = {0};
    char pass[PASS_MAX + 1] = {0};
    // A typed name wins over the dropdown: if the operator bothered to type
    // one, the listed network is not the one they want.
    char manual[SSID_MAX + 1] = {0};
    if (httpd_query_key_value(body, "ssid_manual", manual, sizeof(manual)) == ESP_OK) {
        url_decode(manual);
    }
    // The dropdown is the answer unless it says "Other", in which case the
    // revealed box is. When no networks were found at all the form has no
    // dropdown and posts the typed name as `ssid` directly, which lands in the
    // same place.
    char sel[SSID_MAX + 1] = {0};
    if (httpd_query_key_value(body, "ssid", sel, sizeof(sel)) == ESP_OK) {
        url_decode(sel);
    }
    if (sel[0] != '\0' && strcmp(sel, SSID_OTHER) != 0) {
        strncpy(ssid, sel, sizeof(ssid) - 1);
    } else {
        strncpy(ssid, manual, sizeof(ssid) - 1);
    }
    if (ssid[0] == '\0') {
        return send_error(req, "400 Bad Request",
                          "Network required — pick one from the list, or choose "
                          "<b>Other…</b> and type a name.");
    }
    (void)httpd_query_key_value(body, "pass", pass, sizeof(pass));
    url_decode(pass);

    if (!creds_save(ssid, pass)) {
        return send_error(req, "500 Internal Server Error",
                          "Could not store the credentials in flash.");
    }
    ESP_LOGI(TAG, "Stored credentials for '%s'", ssid);

    // Answer before rebooting: the phone is on our AP, and the AP goes away
    // the moment we switch to station mode. Telling the operator what will
    // happen is the difference between "it worked" and "it hung".
    httpd_resp_set_type(req, HTML_UTF8);
    httpd_resp_sendstr(req,
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Saved</title><body style='font-family:system-ui;margin:2rem'>"
        "<h2>Saved</h2><p>The sidekick is restarting to join that network. "
        "This access point will disappear — reconnect your phone to your own WiFi.</p>");

    // Restart rather than switching mode in place: a clean boot re-runs the
    // stored-credentials path, so there is one join path to get right instead
    // of two.
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;  // not reached
}

// Redirect anything we do not serve back to the form. A phone's connectivity
// probe asks for a specific vendor URL (captive.apple.com/hotspot-detect.html
// and friends) and expects a known body; a 302 to our page is what tells the OS
// this network is captive and pops the sign-in sheet.
static esp_err_t redirect_to_form(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// RFC 8910 DHCP option 114. Modern iOS and Android read the portal URI straight
// from the lease and skip probing entirely, which makes the sheet appear
// faster and more reliably than DNS hijacking alone. Both are wired up because
// older clients only honour one or the other.
static void advertise_captive_portal_uri(void)
{
    if (!s_netif_ap) {
        return;
    }
    esp_netif_ip_info_t ip = {0};
    if (esp_netif_get_ip_info(s_netif_ap, &ip) != ESP_OK) {
        return;
    }
    char uri[32];
    snprintf(uri, sizeof(uri), "http://" IPSTR, IP2STR(&ip.ip));
    // The DHCP server has to be stopped to change options, then restarted.
    esp_netif_dhcps_stop(s_netif_ap);
    const esp_err_t err = esp_netif_dhcps_option(s_netif_ap, ESP_NETIF_OP_SET,
                                                 ESP_NETIF_CAPTIVEPORTAL_URI,
                                                 uri, strlen(uri));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Captive-portal DHCP option not set: %s", esp_err_to_name(err));
    }
    esp_netif_dhcps_start(s_netif_ap);
    ESP_LOGI(TAG, "Captive portal advertised at %s", uri);
}

static void httpd_start_provisioning(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }
    static const httpd_uri_t form = {
        .uri = "/", .method = HTTP_GET, .handler = get_form,
    };
    static const httpd_uri_t provision = {
        .uri = "/provision", .method = HTTP_POST, .handler = post_provision,
    };
    static const httpd_uri_t rescan = {
        .uri = "/rescan", .method = HTTP_GET, .handler = get_rescan,
    };
    httpd_register_uri_handler(s_httpd, &form);
    httpd_register_uri_handler(s_httpd, &rescan);
    httpd_register_uri_handler(s_httpd, &provision);
    httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, redirect_to_form);
}

// Advertised in both modes. On the provisioning AP it is a convenience next to
// the captive portal; once joined it is the only way to reach the sidekick
// without hunting for a DHCP lease.
static void mdns_bring_up(void)
{
    static bool started = false;
    if (started) {
        return;  // mdns_init() twice is an error, and both modes call this
    }
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mDNS init failed");
        return;
    }
    started = true;
    mdns_hostname_set(WIFI_PROV_MDNS_HOST);
    mdns_instance_name_set("Mini-FT8 sidekick");
    // Advertising the service as well as the hostname is what makes the device
    // show up in network browsers, not just resolve when you already know the
    // name.
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: http://" WIFI_PROV_MDNS_HOST ".local/");
}

// ---------------------------------------------------------------------------
// Station-mode status page
// ---------------------------------------------------------------------------

static esp_err_t get_status(httpd_req_t *req)
{
    esp_netif_ip_info_t ip = {0};
    if (s_netif_sta) {
        esp_netif_get_ip_info(s_netif_sta, &ip);
    }
    const esp_app_desc_t *desc = esp_app_get_description();

    char page[1024];
    snprintf(page, sizeof(page),
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Mini-FT8 sidekick</title>"
        "<style>body{font-family:system-ui;margin:2rem auto;max-width:22rem;padding:0 1rem}"
        "dt{color:#666;font-size:.8rem;margin-top:.7rem}dd{margin:0;font-size:1rem}"
        "button{width:100%%;padding:.7rem;font-size:1rem;margin-top:1.5rem}</style>"
        "<h2>Mini-FT8 sidekick</h2>"
        "<dl>"
        "<dt>Network</dt><dd>%s</dd>"
        "<dt>Address</dt><dd>" IPSTR "</dd>"
        "<dt>Firmware</dt><dd>%s</dd>"
        "<dt>Uptime</dt><dd>%lld s</dd>"
        "</dl>"
        "<form method=POST action=/forget "
        "onsubmit=\"return confirm('Forget this network and restart into setup?')\">"
        "<button>Forget WiFi</button></form>",
        s_ssid[0] ? s_ssid : "(unknown)", IP2STR(&ip.ip),
        desc ? desc->version : "?",
        (long long)(esp_timer_get_time() / 1000000));

    httpd_resp_set_type(req, HTML_UTF8);
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t post_forget(httpd_req_t *req)
{
    httpd_resp_set_type(req, HTML_UTF8);
    httpd_resp_sendstr(req,
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Forgotten</title><body style='font-family:system-ui;margin:2rem'>"
        "<h2>Forgotten</h2><p>Restarting into setup. Look for the "
        "<b>MiniFT8-SK-…</b> network.</p>");
    vTaskDelay(pdMS_TO_TICKS(500));
    wifi_prov_forget_and_restart();
    return ESP_OK;  // not reached
}

static void httpd_start_status(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }
    static const httpd_uri_t status = {
        .uri = "/", .method = HTTP_GET, .handler = get_status,
    };
    static const httpd_uri_t forget = {
        .uri = "/forget", .method = HTTP_POST, .handler = post_forget,
    };
    httpd_register_uri_handler(s_httpd, &status);
    httpd_register_uri_handler(s_httpd, &forget);
}

// ---------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------

static void ap_name(char *out, size_t out_len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    // MAC suffix so two sidekicks on one bench are distinguishable.
    snprintf(out, out_len, "MiniFT8-SK-%02X%02X", mac[4], mac[5]);
}

static void start_ap(void)
{
    char ssid[33];
    ap_name(ssid, sizeof(ssid));

    ensure_ap_netif();
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = strlen(ssid);
    cfg.ap.max_connection = 2;
    // Open, deliberately. A fixed password shipped in a public repo is not
    // security, it is the appearance of it, and provisioning only runs when
    // the device has no credentials. Anyone in radio range during that window
    // could set them — a real exposure, judged acceptable for a bench device
    // and recorded rather than glossed. RFC 0001 §5.2d.
    cfg.ap.authmode = WIFI_AUTH_OPEN;

    // APSTA, not AP: scanning requires a station interface, and in plain AP
    // mode esp_wifi_scan_start() fails. The station side stays unconnected —
    // it exists so the provisioning page can list what is in range.
    ensure_sta_netif();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_join_active = false;
    s_ap_active = true;
    advertise_captive_portal_uri();
    mdns_bring_up();

    // Answer every A query with our own address: the phone's connectivity
    // probe then resolves here, gets the form instead of the expected
    // success response, and the OS opens its sign-in sheet.
    static dns_server_config_t dns_cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    s_dns = start_dns_server(&dns_cfg);
    // Scan now, while nothing is connected: the off-channel time cannot drop
    // a client that has not arrived yet.
    scan_refresh();
    httpd_start_provisioning();
    xTaskCreate(scan_keepfresh_task, "scan_fresh", 3072, NULL, 3, NULL);
    ESP_LOGW(TAG, "Provisioning AP '%s' up — open http://192.168.4.1/", ssid);
}

static bool try_join(const char *ssid, const char *pass)
{
    ensure_sta_netif();
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    // Before esp_wifi_start(), not after. Starting the driver posts
    // WIFI_EVENT_STA_START, and the event loop dispatches it from its own
    // higher-priority task -- which can run before this one reaches the next
    // statement. The STA_START handler only calls esp_wifi_connect() when this
    // flag is set (that gate is what stops the fallback AP's station side
    // re-attacking a network that already refused it), so setting it late lost
    // the race: no association was ever started, no event ever arrived, and
    // the wait below sat forever with nothing in the log after "Joining".
    s_join_active = true;
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Joining '%s'...", ssid);
    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_GOT_IP | BIT_JOIN_FAIL,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(JOIN_TIMEOUT_MS));
    const bool joined = (bits & BIT_GOT_IP) != 0;
    if (!joined) {
        if (bits == 0) {
            ESP_LOGE(TAG, "Join timed out after %d s with no result",
                     JOIN_TIMEOUT_MS / 1000);
        }
        s_join_active = false;  // stop the handler re-arming behind us
    }
    return joined;
}

void wifi_prov_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    // One copy of the credentials, and it is ours. The driver defaults to
    // WIFI_STORAGE_FLASH, which quietly persists whatever is handed to
    // esp_wifi_set_config() into its own nvs.net80211 namespace -- a second
    // store this module neither writes nor clears. That makes "forget the
    // network" unimplementable by design: clearing the sidekick keys would
    // leave the driver's copy behind and the device would keep reconnecting
    // to a network it had just been told to forget. RAM storage makes
    // creds_load()/creds_save() the single source of truth.
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &on_wifi_event, NULL, NULL));

    char ssid[SSID_MAX + 1] = {0};
    char pass[PASS_MAX + 1] = {0};
    if (creds_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
        strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
        if (try_join(ssid, pass)) {
            // Joined: serve the status page so the operator can see what it
            // connected to and change their mind, without a serial console.
            mdns_bring_up();
            httpd_start_status();
            return;
        }
        // Stored credentials that no longer work (moved, password changed).
        // Fall through to the AP rather than looping: the operator needs a way
        // back in, and it is the same way they got in the first time.
        ESP_LOGW(TAG, "Stored credentials did not work — falling back to the AP");
        s_ssid[0] = '\0';
        esp_wifi_stop();
    } else {
        ESP_LOGI(TAG, "No stored credentials");
    }
    start_ap();
}

bool wifi_prov_is_online(void) { return s_online; }
const char *wifi_prov_ssid(void) { return s_ssid; }
bool wifi_prov_ap_active(void) { return s_ap_active; }

void wifi_prov_forget_and_restart(void)
{
    if (creds_erase()) {
        ESP_LOGW(TAG, "Credentials forgotten — restarting into provisioning");
    } else {
        ESP_LOGE(TAG, "Could not clear credentials");
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}
