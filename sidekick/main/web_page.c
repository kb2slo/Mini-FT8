// Serves the bootstrap pages from real .html files. See web_page.h for why
// the HTML left the C source, and why values escape by default.

#include <string.h>

#include "web_page.h"

// Longest entity is "&quot;" / "&#39;" at 6 and 5 bytes.
#define ESCAPE_MAX_GROWTH 6u

static const char *entity_for(char c)
{
    switch (c) {
        case '&':  return "&amp;";
        case '<':  return "&lt;";
        case '>':  return "&gt;";
        case '"':  return "&quot;";
        // Escaped as a numeric reference because `&apos;` is XML and was not
        // in HTML 4; the numeric form is understood everywhere.
        case '\'': return "&#39;";
        default:   return NULL;
    }
}

bool web_html_escape(const char *in, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    if (!in) {
        return true;
    }
    size_t w = 0;
    for (size_t i = 0; in[i]; ++i) {
        const char *ent = entity_for(in[i]);
        const size_t need = ent ? strlen(ent) : 1u;
        if (w + need + 1u > out_len) {
            out[0] = '\0';
            return false;
        }
        if (ent) {
            memcpy(out + w, ent, need);
        } else {
            out[w] = in[i];
        }
        w += need;
    }
    out[w] = '\0';
    return true;
}

// Emits `value` escaped, in bounded chunks, without needing a buffer as large
// as the whole escaped result.
static bool emit_escaped(const char *value, web_emit_fn emit, void *ctx)
{
    const char *run = value;
    for (const char *p = value; *p; ++p) {
        const char *ent = entity_for(*p);
        if (!ent) {
            continue;
        }
        if (p > run && !emit(ctx, run, (size_t)(p - run))) {
            return false;
        }
        if (!emit(ctx, ent, strlen(ent))) {
            return false;
        }
        run = p + 1;
    }
    const size_t tail = strlen(run);
    return tail == 0 || emit(ctx, run, tail);
}

static const web_sub_t *find_sub(const web_sub_t *subs, size_t n_subs, const char *key, size_t key_len)
{
    for (size_t i = 0; i < n_subs; ++i) {
        if (subs[i].key && strlen(subs[i].key) == key_len && memcmp(subs[i].key, key, key_len) == 0) {
            return &subs[i];
        }
    }
    return NULL;
}

bool web_page_expand(const char *page, const web_sub_t *subs, size_t n_subs,
                     web_emit_fn emit, void *ctx)
{
    if (!page || !emit) {
        return false;
    }
    const char *run = page;
    const char *p   = page;

    while (*p) {
        if (p[0] != '{' || p[1] != '{') {
            ++p;
            continue;
        }
        const char *key   = p + 2;
        const char *close = strstr(key, "}}");
        const web_sub_t *sub =
            close ? find_sub(subs, n_subs, key, (size_t)(close - key)) : NULL;
        if (!sub) {
            // Unknown key, or no closing braces: pass it through so the page
            // shows the mistake instead of hiding it.
            ++p;
            continue;
        }
        if (p > run && !emit(ctx, run, (size_t)(p - run))) {
            return false;
        }
        const char *value = sub->value ? sub->value : "";
        if (sub->already_html) {
            if (value[0] && !emit(ctx, value, strlen(value))) {
                return false;
            }
        } else if (!emit_escaped(value, emit, ctx)) {
            return false;
        }
        p   = close + 2;
        run = p;
    }

    const size_t tail = strlen(run);
    return tail == 0 || emit(ctx, run, tail);
}

#ifndef HOST_MOCK
static bool emit_to_response(void *ctx, const char *data, size_t len)
{
    return httpd_resp_send_chunk((httpd_req_t *)ctx, data, len) == ESP_OK;
}

esp_err_t web_page_send(httpd_req_t *req, const char *page, const web_sub_t *subs, size_t n_subs)
{
    // Set explicitly rather than left to esp_http_server, whose default for
    // HTML omits the charset and gets guessed as Latin-1 -- which is what
    // mangled the em dashes in these pages once already.
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (!web_page_expand(page, subs, n_subs, emit_to_response, req)) {
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}
#endif
