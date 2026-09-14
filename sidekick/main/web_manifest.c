// Bundle manifest: constrained JSON parse + version/path/digest checks.
// See web_manifest.h and RFC 0004 §4.

#include "web_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool web_manifest_path_ok(const char *relpath)
{
    if (!relpath || relpath[0] == '\0' || relpath[0] == '/' || relpath[0] == '\\') {
        return false;
    }
    if (strlen(relpath) >= WEB_MANIFEST_PATH_MAX) {
        return false;
    }
    if (strstr(relpath, "..") != NULL || strchr(relpath, '\\') != NULL) {
        return false;
    }
    // Keep dotfiles (/.staging, /.manifest) out of the operator-visible tree.
    if (relpath[0] == '.' || strstr(relpath, "/.") != NULL) {
        return false;
    }
    return true;
}

bool web_manifest_sha256_hex_ok(const char *s)
{
    if (!s || strlen(s) != WEB_MANIFEST_SHA256_HEX) {
        return false;
    }
    for (size_t i = 0; i < WEB_MANIFEST_SHA256_HEX; ++i) {
        const char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool web_manifest_api_ok(unsigned api_min, unsigned device_api)
{
    return api_min >= 1u && api_min <= device_api;
}

static void set_err(char *err, size_t err_len, const char *msg)
{
    if (!err || err_len == 0) {
        return;
    }
    snprintf(err, err_len, "%s", msg ? msg : "error");
}

static bool is_json_ws(char c)
{
    switch (c) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            return true;
        default:
            return false;
    }
}

static const char *skip_ws(const char *p)
{
    while (p && *p && is_json_ws(*p)) {
        ++p;
    }
    return p;
}

static bool parse_string(const char **pp, char *out, size_t out_len)
{
    const char *p = skip_ws(*pp);
    if (!p || *p != '"') {
        return false;
    }
    ++p;
    size_t n = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            // Manifest paths and hex digests need no escapes; refuse rather
            // than implement a JSON string decoder we do not need.
            return false;
        }
        if (n + 1 >= out_len) {
            return false;
        }
        out[n++] = *p++;
    }
    if (*p != '"') {
        return false;
    }
    out[n] = '\0';
    *pp = p + 1;
    return true;
}

static bool parse_uint(const char **pp, unsigned long *out)
{
    const char *p = skip_ws(*pp);
    if (!p || *p < '0' || *p > '9') {
        return false;
    }
    char *end = NULL;
    const unsigned long v = strtoul(p, &end, 10);
    if (end == p) {
        return false;
    }
    *out = v;
    *pp = end;
    return true;
}

static bool expect_char(const char **pp, char c)
{
    const char *p = skip_ws(*pp);
    if (!p || *p != c) {
        return false;
    }
    *pp = p + 1;
    return true;
}

bool web_manifest_parse(const char *json, web_manifest_t *out, char *err, size_t err_len)
{
    if (!json || !out) {
        set_err(err, err_len, "null input");
        return false;
    }
    memset(out, 0, sizeof(*out));
    const char *p = skip_ws(json);
    if (!expect_char(&p, '{')) {
        set_err(err, err_len, "expected object");
        return false;
    }

    bool saw_api = false;
    bool saw_assets = false;

    p = skip_ws(p);
    if (*p == '}') {
        set_err(err, err_len, "empty manifest");
        return false;
    }

    while (1) {
        char key[32];
        if (!parse_string(&p, key, sizeof(key)) || !expect_char(&p, ':')) {
            set_err(err, err_len, "bad object key");
            return false;
        }

        if (strcmp(key, "api_min") == 0) {
            unsigned long v = 0;
            if (!parse_uint(&p, &v) || v > 0xfffffffful) {
                set_err(err, err_len, "bad api_min");
                return false;
            }
            out->api_min = (unsigned)v;
            saw_api = true;
        } else if (strcmp(key, "assets") == 0) {
            if (!expect_char(&p, '[')) {
                set_err(err, err_len, "assets not array");
                return false;
            }
            p = skip_ws(p);
            if (*p == ']') {
                ++p;
                saw_assets = true;
            } else {
                while (1) {
                    if (out->n_assets >= WEB_MANIFEST_MAX_ASSETS) {
                        set_err(err, err_len, "too many assets");
                        return false;
                    }
                    web_manifest_asset_t *a = &out->assets[out->n_assets];
                    if (!expect_char(&p, '{')) {
                        set_err(err, err_len, "asset not object");
                        return false;
                    }
                    bool got_path = false, got_sha = false, got_size = false;
                    p = skip_ws(p);
                    while (1) {
                        char ak[16];
                        if (!parse_string(&p, ak, sizeof(ak)) || !expect_char(&p, ':')) {
                            set_err(err, err_len, "bad asset key");
                            return false;
                        }
                        if (strcmp(ak, "path") == 0) {
                            if (!parse_string(&p, a->path, sizeof(a->path))) {
                                set_err(err, err_len, "bad path");
                                return false;
                            }
                            got_path = true;
                        } else if (strcmp(ak, "sha256") == 0) {
                            if (!parse_string(&p, a->sha256_hex, sizeof(a->sha256_hex))) {
                                set_err(err, err_len, "bad sha256");
                                return false;
                            }
                            got_sha = true;
                        } else if (strcmp(ak, "size") == 0) {
                            unsigned long v = 0;
                            if (!parse_uint(&p, &v)) {
                                set_err(err, err_len, "bad size");
                                return false;
                            }
                            a->size = (size_t)v;
                            got_size = true;
                        } else {
                            set_err(err, err_len, "unknown asset field");
                            return false;
                        }
                        p = skip_ws(p);
                        if (*p == ',') {
                            ++p;
                            continue;
                        }
                        if (*p == '}') {
                            ++p;
                            break;
                        }
                        set_err(err, err_len, "asset object");
                        return false;
                    }
                    if (!got_path || !got_sha || !got_size) {
                        set_err(err, err_len, "asset missing field");
                        return false;
                    }
                    if (!web_manifest_path_ok(a->path)) {
                        set_err(err, err_len, "unsafe path");
                        return false;
                    }
                    if (!web_manifest_sha256_hex_ok(a->sha256_hex)) {
                        set_err(err, err_len, "sha256 must be 64 lowercase hex");
                        return false;
                    }
                    // Duplicate paths would make promote order ambiguous.
                    for (size_t i = 0; i < out->n_assets; ++i) {
                        if (strcmp(out->assets[i].path, a->path) == 0) {
                            set_err(err, err_len, "duplicate path");
                            return false;
                        }
                    }
                    ++out->n_assets;
                    p = skip_ws(p);
                    if (*p == ',') {
                        ++p;
                        continue;
                    }
                    if (*p == ']') {
                        ++p;
                        break;
                    }
                    set_err(err, err_len, "assets array");
                    return false;
                }
                saw_assets = true;
            }
        } else {
            set_err(err, err_len, "unknown field");
            return false;
        }

        p = skip_ws(p);
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == '}') {
            ++p;
            break;
        }
        set_err(err, err_len, "object tail");
        return false;
    }

    p = skip_ws(p);
    if (*p != '\0') {
        set_err(err, err_len, "trailing junk");
        return false;
    }
    if (!saw_api || !saw_assets) {
        set_err(err, err_len, "need api_min and assets");
        return false;
    }
    if (out->n_assets == 0) {
        set_err(err, err_len, "assets empty");
        return false;
    }
    // Version skew is refused by web_manifest_api_ok(), not here: parse must
    // succeed so begin can report "api_min too new" plainly.
    return true;
}
