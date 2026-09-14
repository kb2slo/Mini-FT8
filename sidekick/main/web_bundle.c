// Bundle install onto the web FS: stage → verify digests → promote.
// See web_bundle.h and RFC 0004 §4.

#include "web_bundle.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "mbedtls/sha256.h"

#include "pairing_http.h"
#include "web_fs.h"
#include "web_manifest.h"

static const char *TAG = "web_bundle";

#define STAGING_DIR WEB_FS_ROOT "/.staging"
#define MANIFEST_MAX_BYTES 8192
#define ASSET_MAX_BYTES (512u * 1024u)
#define RECV_CHUNK 1024

static web_manifest_t s_manifest;
static bool s_staging;
static bool s_got[WEB_MANIFEST_MAX_ASSETS];

static void abs_staging(char *out, size_t out_len, const char *relpath)
{
    snprintf(out, out_len, "%s/%s", STAGING_DIR, relpath);
}

static void abs_final(char *out, size_t out_len, const char *relpath)
{
    snprintf(out, out_len, "%s/%s", WEB_FS_ROOT, relpath);
}

static void digest_to_hex(const unsigned char dig[32], char out[WEB_MANIFEST_SHA256_HEX + 1])
{
    static const char *hex = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        out[i * 2] = hex[(dig[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[dig[i] & 0xf];
    }
    out[WEB_MANIFEST_SHA256_HEX] = '\0';
}

// Ensure every parent directory of abs_path exists (LittleFS VFS mkdir).
static esp_err_t ensure_parent_dirs(const char *abs_path)
{
    char tmp[160];
    if (strlen(abs_path) >= sizeof(tmp)) {
        return ESP_ERR_INVALID_ARG;
    }
    strcpy(tmp, abs_path);
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) {
        return ESP_OK;
    }
    *slash = '\0';
    // Walk from WEB_FS_ROOT downward creating missing dirs.
    for (char *p = tmp + 1; *p; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
            ESP_LOGE(TAG, "mkdir %s: %d", tmp, errno);
            return ESP_FAIL;
        }
        *p = '/';
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "mkdir %s: %d", tmp, errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t wipe_tree(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        return ESP_OK;  // nothing to wipe
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char child[320];
        const int n = snprintf(child, sizeof(child), "%s/%s", dir, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(child)) {
            continue;
        }
        struct stat st;
        if (stat(child, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            wipe_tree(child);
            rmdir(child);
        } else {
            unlink(child);
        }
    }
    closedir(d);
    return ESP_OK;
}

static void clear_staging_state(void)
{
    s_staging = false;
    memset(&s_manifest, 0, sizeof(s_manifest));
    memset(s_got, 0, sizeof(s_got));
}

static esp_err_t reset_staging(void)
{
    wipe_tree(STAGING_DIR);
    rmdir(STAGING_DIR);
    if (mkdir(STAGING_DIR, 0777) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "mkdir staging failed: %d", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static int find_asset(const char *relpath)
{
    for (size_t i = 0; i < s_manifest.n_assets; ++i) {
        if (strcmp(s_manifest.assets[i].path, relpath) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static bool file_sha256_hex(const char *path, size_t expect_size, char out_hex[WEB_MANIFEST_SHA256_HEX + 1])
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    unsigned char *buf = (unsigned char *)malloc(RECV_CHUNK);
    if (!buf) {
        fclose(f);
        return false;
    }
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts(&ctx, 0) != 0) {
        mbedtls_sha256_free(&ctx);
        free(buf);
        fclose(f);
        return false;
    }
    size_t total = 0;
    while (1) {
        const size_t n = fread(buf, 1, RECV_CHUNK, f);
        if (n == 0) {
            break;
        }
        total += n;
        if (total > expect_size || mbedtls_sha256_update(&ctx, buf, n) != 0) {
            mbedtls_sha256_free(&ctx);
            free(buf);
            fclose(f);
            return false;
        }
    }
    free(buf);
    fclose(f);
    if (total != expect_size) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    unsigned char dig[32];
    if (mbedtls_sha256_finish(&ctx, dig) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    mbedtls_sha256_free(&ctx);
    digest_to_hex(dig, out_hex);
    return true;
}

static esp_err_t read_body_bounded(httpd_req_t *req, char *buf, size_t cap, size_t *out_len)
{
    if (req->content_len >= cap) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r <= 0) {
            return ESP_FAIL;
        }
        got += (size_t)r;
    }
    buf[got] = '\0';
    *out_len = got;
    return ESP_OK;
}

static esp_err_t drain_body(httpd_req_t *req)
{
    char discard[64];
    while (httpd_req_recv(req, discard, sizeof(discard)) > 0) {
    }
    return ESP_OK;
}

static esp_err_t post_begin(httpd_req_t *req)
{
    if (!web_fs_ready()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "web fs not ready");
        return ESP_FAIL;
    }

    char *body = (char *)malloc(MANIFEST_MAX_BYTES);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    size_t body_len = 0;
    const esp_err_t rerr = read_body_bounded(req, body, MANIFEST_MAX_BYTES, &body_len);
    if (rerr != ESP_OK) {
        free(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "manifest too large or incomplete");
        return ESP_FAIL;
    }

    // Heap, not stack: web_manifest_t is several KB and the httpd task stack
    // is only 4 KB by default — a stack parse panics the device on begin.
    web_manifest_t *m = (web_manifest_t *)calloc(1, sizeof(*m));
    if (!m) {
        free(body);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    char err[80];
    if (!web_manifest_parse(body, m, err, sizeof(err))) {
        free(body);
        free(m);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, err);
        return ESP_FAIL;
    }
    free(body);

    if (!web_manifest_api_ok(m->api_min, WEB_API_VERSION)) {
        char msg[96];
        snprintf(msg, sizeof(msg), "api_min %u requires firmware API %u (have %u)", m->api_min,
                 m->api_min, WEB_API_VERSION);
        free(m);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
        return ESP_FAIL;
    }

    if (reset_staging() != ESP_OK) {
        free(m);
        clear_staging_state();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "staging reset failed");
        return ESP_FAIL;
    }

    s_manifest = *m;
    free(m);
    memset(s_got, 0, sizeof(s_got));
    s_staging = true;
    ESP_LOGI(TAG, "begin: api_min=%u assets=%u", s_manifest.api_min, (unsigned)s_manifest.n_assets);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static bool query_path(httpd_req_t *req, char *out, size_t out_len)
{
    char q[160];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        return false;
    }
    if (httpd_query_key_value(q, "path", out, out_len) != ESP_OK) {
        return false;
    }
    return web_manifest_path_ok(out);
}

static esp_err_t put_file(httpd_req_t *req)
{
    if (!s_staging) {
        drain_body(req);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "call /api/bundle/begin first");
        return ESP_FAIL;
    }

    char rel[WEB_MANIFEST_PATH_MAX];
    if (!query_path(req, rel, sizeof(rel))) {
        drain_body(req);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing or unsafe path=");
        return ESP_FAIL;
    }
    const int idx = find_asset(rel);
    if (idx < 0) {
        drain_body(req);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "path not in manifest");
        return ESP_FAIL;
    }
    const web_manifest_asset_t *a = &s_manifest.assets[idx];
    if (req->content_len != a->size) {
        drain_body(req);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content-Length must match size");
        return ESP_FAIL;
    }
    if (a->size > ASSET_MAX_BYTES) {
        drain_body(req);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "asset too large");
        return ESP_FAIL;
    }

    char path[160];
    abs_staging(path, sizeof(path), rel);
    if (ensure_parent_dirs(path) != ESP_OK) {
        drain_body(req);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "mkdir failed");
        return ESP_FAIL;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        drain_body(req);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open staging failed");
        return ESP_FAIL;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts(&ctx, 0) != 0) {
        mbedtls_sha256_free(&ctx);
        fclose(f);
        unlink(path);
        drain_body(req);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "hash init failed");
        return ESP_FAIL;
    }

    size_t remaining = a->size;
    unsigned char *chunk = (unsigned char *)malloc(RECV_CHUNK);
    if (!chunk) {
        mbedtls_sha256_free(&ctx);
        fclose(f);
        unlink(path);
        drain_body(req);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    while (remaining > 0) {
        const size_t want = remaining < RECV_CHUNK ? remaining : RECV_CHUNK;
        const int r = httpd_req_recv(req, (char *)chunk, want);
        if (r <= 0) {
            free(chunk);
            mbedtls_sha256_free(&ctx);
            fclose(f);
            unlink(path);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "incomplete body");
            return ESP_FAIL;
        }
        if (fwrite(chunk, 1, (size_t)r, f) != (size_t)r ||
            mbedtls_sha256_update(&ctx, chunk, (size_t)r) != 0) {
            free(chunk);
            mbedtls_sha256_free(&ctx);
            fclose(f);
            unlink(path);
            drain_body(req);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
            return ESP_FAIL;
        }
        remaining -= (size_t)r;
    }
    free(chunk);
    if (fclose(f) != 0) {
        mbedtls_sha256_free(&ctx);
        unlink(path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "close failed");
        return ESP_FAIL;
    }

    unsigned char dig[32];
    if (mbedtls_sha256_finish(&ctx, dig) != 0) {
        mbedtls_sha256_free(&ctx);
        unlink(path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "hash finish failed");
        return ESP_FAIL;
    }
    mbedtls_sha256_free(&ctx);

    char hex[WEB_MANIFEST_SHA256_HEX + 1];
    digest_to_hex(dig, hex);
    if (strcmp(hex, a->sha256_hex) != 0) {
        unlink(path);
        s_got[idx] = false;
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "sha256 mismatch");
        return ESP_FAIL;
    }

    s_got[idx] = true;
    ESP_LOGI(TAG, "staged %s (%u bytes)", rel, (unsigned)a->size);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t promote_one(const web_manifest_asset_t *a)
{
    char src[160], dst[160];
    abs_staging(src, sizeof(src), a->path);
    abs_final(dst, sizeof(dst), a->path);

    char hex[WEB_MANIFEST_SHA256_HEX + 1];
    if (!file_sha256_hex(src, a->size, hex) || strcmp(hex, a->sha256_hex) != 0) {
        ESP_LOGE(TAG, "verify failed for %s", a->path);
        return ESP_FAIL;
    }
    if (ensure_parent_dirs(dst) != ESP_OK) {
        return ESP_FAIL;
    }
    unlink(dst);  // rename onto existing may fail on LittleFS
    if (rename(src, dst) != 0) {
        ESP_LOGE(TAG, "promote rename %s -> %s failed: %d", src, dst, errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t post_commit(httpd_req_t *req)
{
    drain_body(req);
    if (!s_staging) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "call /api/bundle/begin first");
        return ESP_FAIL;
    }
    for (size_t i = 0; i < s_manifest.n_assets; ++i) {
        if (!s_got[i]) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing staged asset");
            return ESP_FAIL;
        }
    }
    for (size_t i = 0; i < s_manifest.n_assets; ++i) {
        if (promote_one(&s_manifest.assets[i]) != ESP_OK) {
            // Staging remains; live tree may be partially updated — operator
            // force-rehydrate recovers seed pages. Say so plainly.
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "promote failed");
            return ESP_FAIL;
        }
    }
    wipe_tree(STAGING_DIR);
    rmdir(STAGING_DIR);
    ESP_LOGI(TAG, "promoted %u assets", (unsigned)s_manifest.n_assets);
    clear_staging_state();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t web_bundle_register(httpd_handle_t server)
{
    if (!server) {
        return ESP_ERR_INVALID_ARG;
    }
    static const httpd_uri_t begin = {
        .uri = "/api/bundle/begin",
        .method = HTTP_POST,
        .handler = post_begin,
    };
    static const httpd_uri_t file = {
        .uri = "/api/bundle/file",
        .method = HTTP_PUT,
        .handler = put_file,
    };
    static const httpd_uri_t commit = {
        .uri = "/api/bundle/commit",
        .method = HTTP_POST,
        .handler = post_commit,
    };
    const struct {
        const httpd_uri_t *uri;
    } routes[] = {{&begin}, {&file}, {&commit}};
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        const esp_err_t err = pairing_http_register(server, routes[i].uri, PAIRING_REQUIRED);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
