// LittleFS web root: mount, hydrate from EMBED_TXTFILES, load for HTTP.
// See web_fs.h and RFC 0004 §3.
//
// Seed membership comes from main/web/ via the generated web_seed_table — not
// a handwritten list in this file.

#include "web_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_littlefs.h"

#include "pairing_http.h"
#include "pairing_http_cap.h"
#include "web_seed_table.h"

static const char *TAG = "web_fs";

static bool s_ready;

static void abs_path(char *out, size_t out_len, const char *relpath)
{
    snprintf(out, out_len, "%s/%s", WEB_FS_ROOT, relpath);
}

// Relative path under the web root: no leading slash, no "..", no backslash.
static bool relpath_ok(const char *relpath)
{
    if (!relpath || relpath[0] == '\0' || relpath[0] == '/' || relpath[0] == '\\') {
        return false;
    }
    if (strstr(relpath, "..") != NULL || strchr(relpath, '\\') != NULL) {
        return false;
    }
    return true;
}

static bool file_matches_embed(const char *path, const char *embed, size_t len)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || (size_t)st.st_size != len) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    char *buf = (char *)malloc(len);
    if (!buf) {
        fclose(f);
        return false;
    }
    const size_t n = fread(buf, 1, len, f);
    fclose(f);
    const bool ok = (n == len) && (memcmp(buf, embed, len) == 0);
    free(buf);
    return ok;
}

static esp_err_t write_seed_file(const char *path, const char *embed, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "open %s for write failed", path);
        return ESP_FAIL;
    }
    if (len > 0 && fwrite(embed, 1, len, f) != len) {
        ESP_LOGE(TAG, "write %s failed", path);
        fclose(f);
        return ESP_FAIL;
    }
    if (fclose(f) != 0) {
        ESP_LOGE(TAG, "close %s failed", path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t hydrate_seeds(bool force)
{
    int wrote = 0;
    for (size_t i = 0; i < web_seed_files_count; ++i) {
        const web_seed_file_t *s = &web_seed_files[i];
        char path[96];
        abs_path(path, sizeof(path), s->name);
        if (!force && file_matches_embed(path, s->embed, s->size)) {
            continue;
        }
        const esp_err_t err = write_seed_file(path, s->embed, s->size);
        if (err != ESP_OK) {
            return err;
        }
        ++wrote;
        ESP_LOGI(TAG, "%s %s (%u bytes)", force ? "rehydrated" : "hydrated", s->name,
                 (unsigned)s->size);
    }
    if (wrote == 0) {
        ESP_LOGI(TAG, "seed up to date (%u files)", (unsigned)web_seed_files_count);
    }
    return ESP_OK;
}

esp_err_t web_fs_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_vfs_littlefs_conf_t conf = {
        .base_path = WEB_FS_ROOT,
        .partition_label = "web",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    if (esp_littlefs_info("web", &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "mounted %s (%u / %u bytes used)", WEB_FS_ROOT, (unsigned)used,
                 (unsigned)total);
    }

    err = hydrate_seeds(false);
    if (err != ESP_OK) {
        return err;
    }
    s_ready = true;
    return ESP_OK;
}

bool web_fs_ready(void)
{
    return s_ready;
}

esp_err_t web_fs_rehydrate(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return hydrate_seeds(true);
}

static esp_err_t post_rehydrate(httpd_req_t *req)
{
    char discard[64];
    while (httpd_req_recv(req, discard, sizeof(discard)) > 0) {
    }
    ESP_LOGI(TAG, "force rehydrate requested");
    const esp_err_t err = web_fs_rehydrate();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "force rehydrate failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rehydrate failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "force rehydrate done");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t web_fs_register_rehydrate(httpd_handle_t server)
{
    static const httpd_uri_t route = {
        .uri = "/api/web/rehydrate",
        .method = HTTP_POST,
        .handler = post_rehydrate,
    };
    _Static_assert(PAIRING_ROUTES_WEB_FS_STATION == 2, "station: rehydrate + static");
    _Static_assert(PAIRING_ROUTES_WEB_FS_AP == 1, "AP: static only");
    return pairing_http_register(server, &route, PAIRING_REQUIRED);
}

char *web_fs_load(const char *relpath, size_t *size_out)
{
    if (!s_ready || !relpath_ok(relpath)) {
        return NULL;
    }
    char path[96];
    abs_path(path, sizeof(path), relpath);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    const long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    // Bound runaway reads; seed pages are tens of KB and the app budget is
    // hundreds. A corrupt length should not allocate the partition size.
    if ((size_t)sz > 512u * 1024u) {
        ESP_LOGE(TAG, "%s too large (%ld)", path, sz);
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    const size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[sz] = '\0';
    if (size_out) {
        *size_out = (size_t)sz;
    }
    return buf;
}

static const char *mime_for(const char *relpath)
{
    const char *dot = strrchr(relpath, '.');
    if (!dot) {
        return "application/octet-stream";
    }
    if (strcmp(dot, ".html") == 0) {
        return "text/html; charset=utf-8";
    }
    if (strcmp(dot, ".js") == 0) {
        return "application/javascript; charset=utf-8";
    }
    if (strcmp(dot, ".css") == 0) {
        return "text/css; charset=utf-8";
    }
    if (strcmp(dot, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcmp(dot, ".json") == 0) {
        return "application/json";
    }
    if (strcmp(dot, ".png") == 0) {
        return "image/png";
    }
    if (strcmp(dot, ".ico") == 0) {
        return "image/x-icon";
    }
    return "application/octet-stream";
}

// Serves GET /<path> from the web FS when a more specific route did not match.
// Register last. Exact handlers (/api/..., /, /log) must be registered first
// and win because uri_match_wildcard does strcmp for patterns without '*'.
static esp_err_t get_static(httpd_req_t *req)
{
    const char *uri = req->uri;
    if (!uri || uri[0] != '/') {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    }
    // Strip query string if present.
    const char *q = strchr(uri, '?');
    size_t len = q ? (size_t)(q - uri) : strlen(uri);
    if (len < 2 || len >= 96) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    }
    char rel[96];
    memcpy(rel, uri + 1, len - 1);
    rel[len - 1] = '\0';

    size_t body_len = 0;
    char *body = web_fs_load(rel, &body_len);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    }
    httpd_resp_set_type(req, mime_for(rel));
    // Seed updates on reflash/hydrate, or a desk_push bundle push; do not
    // keep a stale browser copy. no-cache (was here before) still permits a
    // conditional-revalidation cache entry in principle -- moot without an
    // ETag/Last-Modified for the browser to revalidate against, but no-store
    // says it plainly and is also one of the signals browsers check before
    // allowing a page into the back-forward cache, which no-cache does not
    // reliably block (see app.html's own pageshow handler for the other
    // half of that: a bfcache restore skips a network request altogether,
    // so no Cache-Control header sees it at all).
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const esp_err_t err = httpd_resp_send(req, body, body_len);
    free(body);
    return err;
}

esp_err_t web_fs_register_static(httpd_handle_t server)
{
    static const httpd_uri_t route = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = get_static,
    };
    // Open: same as reading any other page on the device (RFC 0004 §7).
    return pairing_http_register(server, &route, PAIRING_OPEN);
}
