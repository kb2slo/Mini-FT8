// Bundle-host base URL in NVS (RFC 0004 §4). See bundle_host.h.

#include "bundle_host.h"

#include <stdio.h>
#include <string.h>

bool bundle_host_url_ok(const char *url)
{
    if (!url || url[0] == '\0') {
        return false;
    }
    const size_t n = strlen(url);
    if (n < 8 || n >= BUNDLE_HOST_MAX) {
        return false;
    }
    if (strncmp(url, "https://", 8) != 0) {
        return false;
    }
    if (url[n - 1] == '/') {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const char c = url[i];
        switch (c) {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
            case '"':
            case '\\':
                return false;
            default:
                break;
        }
    }
    return true;
}

#ifndef HOST_MOCK

#include "esp_log.h"
#include "nvs.h"

#include "pairing_http.h"
#include "pairing_http_cap.h"

static const char *TAG = "bundle_host";

#define NVS_NAMESPACE "sidekick"
#define NVS_KEY_URL   "bundle_host"

static char s_url[BUNDLE_HOST_MAX];
static bool s_ready;

esp_err_t bundle_host_init(void)
{
    memset(s_url, 0, sizeof(s_url));
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_url);
        if (nvs_get_str(h, NVS_KEY_URL, s_url, &len) == ESP_OK && bundle_host_url_ok(s_url)) {
            nvs_close(h);
            s_ready = true;
            ESP_LOGI(TAG, "loaded %s", s_url);
            return ESP_OK;
        }
        nvs_close(h);
        memset(s_url, 0, sizeof(s_url));
    }
    if (!bundle_host_url_ok(BUNDLE_HOST_DEFAULT)) {
        ESP_LOGE(TAG, "BUNDLE_HOST_DEFAULT is invalid");
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_url, sizeof(s_url), "%s", BUNDLE_HOST_DEFAULT);
    s_ready = true;
    ESP_LOGI(TAG, "default %s", s_url);
    return ESP_OK;
}

const char *bundle_host_get(void)
{
    return s_ready ? s_url : BUNDLE_HOST_DEFAULT;
}

esp_err_t bundle_host_set(const char *url)
{
    if (!bundle_host_url_ok(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return ESP_FAIL;
    }
    const esp_err_t err = nvs_set_str(h, NVS_KEY_URL, url);
    if (err == ESP_OK) {
        (void)nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        return err;
    }
    snprintf(s_url, sizeof(s_url), "%s", url);
    s_ready = true;
    ESP_LOGI(TAG, "stored %s", s_url);
    return ESP_OK;
}

static esp_err_t get_bundle_host(httpd_req_t *req)
{
    char body[BUNDLE_HOST_MAX + 32];
    const int n = snprintf(body, sizeof(body), "{\"url\":\"%s\"}", bundle_host_get());
    if (n <= 0 || (size_t)n >= sizeof(body)) {
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, n);
}

static esp_err_t put_bundle_host(httpd_req_t *req)
{
    char body[BUNDLE_HOST_MAX];
    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "url length");
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "incomplete");
            return ESP_FAIL;
        }
        got += (size_t)r;
    }
    body[got] = '\0';
    while (got > 0) {
        const char c = body[got - 1];
        if (c != '\n' && c != '\r' && c != ' ' && c != '\t') {
            break;
        }
        body[--got] = '\0';
    }
    if (bundle_host_set(body) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid url");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t bundle_host_register(httpd_handle_t server)
{
    if (!server) {
        return ESP_ERR_INVALID_ARG;
    }
    static const httpd_uri_t get_uri = {
        .uri = "/api/bundle-host",
        .method = HTTP_GET,
        .handler = get_bundle_host,
    };
    static const httpd_uri_t put_uri = {
        .uri = "/api/bundle-host",
        .method = HTTP_PUT,
        .handler = put_bundle_host,
    };
    const struct {
        const httpd_uri_t *uri;
        pairing_policy_t policy;
    } routes[] = {
        { &get_uri, PAIRING_OPEN },
        { &put_uri, PAIRING_REQUIRED },
    };
    _Static_assert(sizeof(routes) / sizeof(routes[0]) == PAIRING_ROUTES_BUNDLE_HOST,
                   "bundle_host route count");
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        const esp_err_t err = pairing_http_register(server, routes[i].uri, routes[i].policy);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

#endif  // HOST_MOCK
