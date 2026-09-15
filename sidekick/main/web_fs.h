#pragma once

// web_fs.h -- LittleFS web root (RFC 0004 §3).
//
// Partition label `web` mounts at WEB_FS_ROOT. Files under main/web/ are the
// hydrate seed (CMake globs that directory — drop a file in, it is embedded
// and hydrated). After hydrate, HTTP serves from the FS; embed is not a live
// second backend. GET /* serves any path that exists on the FS (register last).
//
// Force re-hydrate (present-but-broken recovery) is web_fs_rehydrate(),
// exposed as token-guarded POST /api/web/rehydrate. Restores seed files from
// the firmware embed only; promoted app assets are left alone. USB-C reflash
// remains the hard floor if the recovery UI itself is unreachable.

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEB_FS_ROOT "/web"

// Mount LittleFS on partition "web", format if empty/corrupt, then hydrate
// seed files from the firmware embed. Safe to call once at boot before any
// HTTP handler runs.
esp_err_t web_fs_init(void);

// True after a successful web_fs_init().
bool web_fs_ready(void);

// Rewrite every seed file from the firmware embed, even if the on-disk copy
// already matches. Used for recovery; ordinary boots use web_fs_init()'s
// compare-and-write.
esp_err_t web_fs_rehydrate(void);

// POST /api/web/rehydrate — PAIRING_REQUIRED. Call before the static GET /*.
esp_err_t web_fs_register_rehydrate(httpd_handle_t server);

// Load a file under WEB_FS_ROOT into a malloc'd buffer. `relpath` is relative
// (e.g. "status.html"). If size_out is non-NULL it receives the byte length
// (no trailing NUL counted). The buffer is always NUL-terminated for text
// convenience; caller frees. NULL on error.
char *web_fs_load(const char *relpath, size_t *size_out);

// Register GET /* to serve files from the web FS. Call after every specific
// route. Requires httpd_config.uri_match_fn = httpd_uri_match_wildcard.
esp_err_t web_fs_register_static(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
