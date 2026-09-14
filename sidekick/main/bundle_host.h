#pragma once

// bundle_host.h -- NVS-stored HTTPS base URL for the phone-relayed app bundle
// (RFC 0004 §4). Soft identity: a rename changes the build-time default and
// optionally an NVS write; it is not a wire-protocol name.

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Default Pages prefix for this repo. No trailing slash. Overridable in NVS;
// do not treat this string as a permanent product identity.
#ifndef BUNDLE_HOST_DEFAULT
#define BUNDLE_HOST_DEFAULT "https://kb2slo.github.io/Mini-FT8/app"
#endif

#define BUNDLE_HOST_MAX 128

// True if url is an https:// base we will store: non-empty, fits, no spaces,
// no CR/LF, does not end with '/'.
bool bundle_host_url_ok(const char *url);

#ifndef HOST_MOCK
#include "esp_err.h"
#include "esp_http_server.h"

// Load from NVS or fall back to BUNDLE_HOST_DEFAULT. Call after NVS is ready.
esp_err_t bundle_host_init(void);

// Current base (never NULL after init; points at a static buffer).
const char *bundle_host_get(void);

// Validate, persist, and update the in-RAM copy. Token-guard the HTTP write.
esp_err_t bundle_host_set(const char *url);

// GET /api/bundle-host (open) and PUT /api/bundle-host (pairing required).
esp_err_t bundle_host_register(httpd_handle_t server);
#endif

#ifdef __cplusplus
}
#endif
