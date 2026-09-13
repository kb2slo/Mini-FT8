#pragma once

// web_page.h -- serve the bootstrap pages from real .html files.
//
// The sidekick's pages used to be C string literals. That cost more than
// looks: the CSS had to be written `width:100%%` to survive snprintf, which
// means no formatter, validator, linter or browser could be pointed at the
// source; a page split across four fragments could not be read as a document;
// and every edit was a C edit. The files now live in `web/` and arrive in the
// image through `EMBED_TXTFILES`, so they are editable and tool-checkable, and
// this module is the small amount of machinery that makes that possible.
//
// Placeholders are `{{name}}`: valid HTML, so a file still opens standalone in
// a browser with the placeholder showing as text. No printf, so no `%%`.
//
// **Values are HTML-escaped by default, and that is a fix rather than a
// nicety.** The old status page interpolated the stored SSID raw, and an SSID
// is not the operator's own text -- it is whatever a nearby access point
// broadcast, chosen from a scan list. A network named with a `<script>` tag,
// once selected, executed on the status page's origin, which is the origin
// that now holds the pairing token in localStorage. Escaping is therefore the
// default and passing HTML through takes a deliberate flag.
//
// The expansion core takes an emit callback rather than an httpd_req_t so it
// is host-tested against a string buffer; the four-line HTTP adapter is the
// only part that needs hardware.

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *key;    // the name between the braces, without them
    const char *value;  // NULL is treated as empty

    // Set only for a value that is already HTML built elsewhere -- the scan
    // list's `<option>` block, whose SSIDs are escaped per item as they are
    // rendered. Anything else escapes.
    bool already_html;
} web_sub_t;

// Receives each run of output. Returns false to abort the whole expansion,
// which is what a dropped connection looks like.
typedef bool (*web_emit_fn)(void *ctx, const char *data, size_t len);

// Streams `page`, replacing every `{{key}}` found in `subs`.
//
// An **unknown** placeholder is emitted verbatim rather than dropped: a typo
// then shows up as `{{ssdi}}` on the page instead of silently rendering an
// empty field, and the raw file in a browser looks the same as the served one.
// An unterminated `{{` is also emitted verbatim, since the alternative is
// discarding the rest of a page over a typo.
bool web_page_expand(const char *page, const web_sub_t *subs, size_t n_subs,
                     web_emit_fn emit, void *ctx);

// Escapes `&<>"'` into entities. False, with `out` left empty, if `out` is too
// small -- never a truncation, which could cut an entity in half or, worse,
// cut off the closing quote of an attribute. Worst-case growth is 6x plus the
// terminator (`&quot;`).
bool web_html_escape(const char *in, char *out, size_t out_len);

#ifndef HOST_MOCK
#include "esp_err.h"
#include "esp_http_server.h"

// Sets the content type and streams `page` to the client with substitutions
// applied, chunked so no buffer has to hold the expanded document. Ends the
// response on success.
esp_err_t web_page_send(httpd_req_t *req, const char *page, const web_sub_t *subs, size_t n_subs);
#endif

#ifdef __cplusplus
}
#endif
