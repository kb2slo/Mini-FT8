// The ESP-IDF half of RFC 0004 §7's pairing token. See pairing_http.h for the
// split, and pairing_token.h for why reads are open on principle.

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "pairing_http.h"

static const char *TAG = "pairing";

// Shares wifi_prov.c's namespace so one nvs_flash_erase clears the whole
// station identity together -- credentials and pairing are one unit to the
// operator, and splitting them would let a reset leave half of it behind.
#define NVS_NAMESPACE "sidekick"
#define NVS_KEY_TOKEN "pair_token"

// Long enough to walk to the radio, press the button and read the value off a
// phone; short enough that a window left open by accident closes itself.
#define DISCLOSURE_WINDOW_US (120 * 1000 * 1000)

// One slot per registered route. Sized with headroom over the eight routes
// that exist across both modes; registration fails loudly rather than
// silently dropping a route, because a dropped route is a missing endpoint and
// a dropped *guard* would be worse.
#define MAX_ROUTES 16

typedef struct {
    esp_err_t (*handler)(httpd_req_t *req);
    void             *user_ctx;
    pairing_policy_t  policy;
} route_t;

static route_t s_routes[MAX_ROUTES];
static size_t  s_route_count;

static char s_token[PAIRING_TOKEN_BUF];

// Written from the button poll, read from the HTTP task. A plain bool is a
// single aligned byte on this target, so no lock is needed; the alternative --
// comparing a 64-bit deadline read from another task -- is the version that
// would need one.
static volatile bool      s_disclosure_open;
static esp_timer_handle_t s_disclosure_timer;

static void disclosure_close(const char *why)
{
    if (s_disclosure_timer) {
        (void)esp_timer_stop(s_disclosure_timer);
    }
    if (s_disclosure_open) {
        s_disclosure_open = false;
        ESP_LOGI(TAG, "Disclosure window closed (%s)", why);
    }
}

static void disclosure_expired(void *arg)
{
    (void)arg;
    disclosure_close("timed out");
}

static bool token_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t       n  = sizeof(s_token);
    const bool   ok = nvs_get_str(h, NVS_KEY_TOKEN, s_token, &n) == ESP_OK;
    nvs_close(h);
    if (!ok || !pairing_token_is_well_formed(s_token)) {
        // A truncated or hand-edited value is discarded rather than used.
        // pairing_allows() would refuse it anyway, but leaving it in place
        // would mean a device that refuses every write with no way to notice
        // why; minting over it is recoverable and visible in the log.
        s_token[0] = '\0';
        return false;
    }
    return true;
}

static bool token_mint_and_store(void)
{
    uint8_t bytes[PAIRING_TOKEN_BYTES];
    // esp_fill_random() is the hardware RNG, seeded from the RF front end once
    // WiFi is up. Not rand(): a predictable token is the same as no token.
    esp_fill_random(bytes, sizeof(bytes));
    pairing_token_format(bytes, s_token);
    memset(bytes, 0, sizeof(bytes));

    if (!pairing_token_is_well_formed(s_token)) {
        s_token[0] = '\0';
        return false;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        s_token[0] = '\0';
        return false;
    }
    const bool ok = nvs_set_str(h, NVS_KEY_TOKEN, s_token) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (!ok) {
        // Keeping a token that did not persist would authorize this boot and
        // refuse the next one, which is the confusing failure. Refuse now.
        s_token[0] = '\0';
        return false;
    }
    return true;
}

esp_err_t pairing_http_init(void)
{
    const esp_timer_create_args_t args = {
        .callback = disclosure_expired,
        .name     = "pair_disclose",
    };
    if (!s_disclosure_timer && esp_timer_create(&args, &s_disclosure_timer) != ESP_OK) {
        ESP_LOGE(TAG, "disclosure timer create failed");
        return ESP_FAIL;
    }

    if (token_load()) {
        ESP_LOGI(TAG, "Pairing token loaded");
        return ESP_OK;
    }
    if (token_mint_and_store()) {
        // Deliberately not logged. The sidekick's USB-C console is where the
        // ADV's log lands (I28a), so anything printed here is visible to
        // whoever is watching that stream -- and the token is the one value
        // whose disclosure is supposed to cost a button press.
        ESP_LOGI(TAG, "Pairing token minted; press the button to read it");
        return ESP_OK;
    }
    ESP_LOGE(TAG, "No pairing token: guarded routes will refuse");
    return ESP_FAIL;
}

void pairing_http_open_disclosure(void)
{
    if (!s_disclosure_timer) {
        return;
    }
    (void)esp_timer_stop(s_disclosure_timer);
    if (esp_timer_start_once(s_disclosure_timer, DISCLOSURE_WINDOW_US) != ESP_OK) {
        return;
    }
    s_disclosure_open = true;
    ESP_LOGI(TAG, "Disclosure window open for %d s", DISCLOSURE_WINDOW_US / 1000000);
}

// Gathers the request's token, if it carried one, and makes the single
// authorization call. Header only -- see pairing_token.h on why reading a form
// field here would steal the body from the handler.
static bool request_allowed(httpd_req_t *req, pairing_policy_t policy)
{
    char  header[PAIRING_TOKEN_BUF + 8];
    char  presented[PAIRING_TOKEN_BUF];
    char *token = NULL;

    const size_t len = httpd_req_get_hdr_value_len(req, PAIRING_TOKEN_HEADER);
    if (len > 0 && len < sizeof(header) &&
        httpd_req_get_hdr_value_str(req, PAIRING_TOKEN_HEADER, header, sizeof(header)) == ESP_OK &&
        pairing_token_from_header(header, presented)) {
        token = presented;
    }
    return pairing_allows(policy, s_token, token);
}

static esp_err_t dispatch(httpd_req_t *req)
{
    route_t *route = (route_t *)req->user_ctx;
    if (!route || !route->handler) {
        return ESP_FAIL;
    }
    if (!request_allowed(req, route->policy)) {
        ESP_LOGW(TAG, "Refused %s (no valid token)", req->uri);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"error\":\"pairing required\"}");
    }
    // Hand the handler the context it was registered with, not ours.
    req->user_ctx = route->user_ctx;
    return route->handler(req);
}

esp_err_t pairing_http_register(httpd_handle_t server, const httpd_uri_t *uri, pairing_policy_t policy)
{
    if (!server || !uri || !uri->handler) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_route_count >= MAX_ROUTES) {
        ESP_LOGE(TAG, "route table full, refusing %s", uri->uri);
        return ESP_ERR_NO_MEM;
    }
    route_t *route = &s_routes[s_route_count];
    route->handler  = uri->handler;
    route->user_ctx = uri->user_ctx;
    route->policy   = policy;

    // esp_http_server copies the descriptor, but keeps our user_ctx pointer --
    // hence the static table rather than a stack temporary.
    httpd_uri_t wrapped = *uri;
    wrapped.handler  = dispatch;
    wrapped.user_ctx = route;

    const esp_err_t err = httpd_register_uri_handler(server, &wrapped);
    if (err != ESP_OK) {
        return err;
    }
    s_route_count++;
    return ESP_OK;
}

// Serves the token while the window is open. 404 when shut, not 403: a closed
// window should look like a route that does not exist, so scanning the device
// does not advertise that there is a token to go looking for.
//
// The first successful GET claims the window and closes it. Re-auth is one
// retrieval, not a 120 s broadcast: any other client that polls afterward
// sees 404. The NVS token itself is unchanged -- rotating it here would log
// out every already-paired browser.
static esp_err_t get_pairing_token(httpd_req_t *req)
{
    if (!s_disclosure_open || !pairing_token_is_well_formed(s_token)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "closed");
        return ESP_FAIL;
    }
    // Claim before sending so two concurrent polls cannot both win.
    disclosure_close("token retrieved");

    char body[PAIRING_TOKEN_BUF + 16];
    const int n = snprintf(body, sizeof(body), "{\"token\":\"%s\"}", s_token);
    if (n <= 0 || (size_t)n >= sizeof(body)) {
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    // Nothing may cache the one response that carries the token.
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, n);
}

// pairing.js, embedded next to the HTML pages. OPEN: it carries no secret, and
// every page loads it so re-auth is one shared path rather than a per-page
// prompt() that only some screens remembered to offer.
extern const char pairing_js_start[] asm("_binary_pairing_js_start");

static esp_err_t get_pairing_js(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    // Firmware-bound: a reflash can change the script, so do not let a browser
    // keep an older copy across updates.
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, pairing_js_start, HTTPD_RESP_USE_STRLEN);
}

esp_err_t pairing_http_register_disclosure(httpd_handle_t server)
{
    static const httpd_uri_t disclosure = {
        .uri = "/api/pairing-token", .method = HTTP_GET, .handler = get_pairing_token,
    };
    static const httpd_uri_t script = {
        .uri = "/pairing.js", .method = HTTP_GET, .handler = get_pairing_js,
    };
    // PAIRING_OPEN, and this is the one route where that needs explaining:
    // requiring the token to read the token is the chicken-and-egg. What gates
    // it is the physical button, checked inside the handler, plus the window
    // closing itself after one successful retrieval (or on timeout).
    esp_err_t err = pairing_http_register(server, &disclosure, PAIRING_OPEN);
    if (err != ESP_OK) {
        return err;
    }
    return pairing_http_register(server, &script, PAIRING_OPEN);
}
