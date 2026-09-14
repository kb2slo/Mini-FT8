#pragma once

// web_manifest.h -- bundle manifest parse + API version gate (RFC 0004 §4).
//
// Dependency-free so host_mock can pin refuse-incompatible and path/digest
// shape without flash. Device install (stage/verify/promote) lives elsewhere.

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Sidekick HTTP/bundle API version. Bump when a breaking change lands in the
// control surface the downloaded app expects. Bundles declare api_min; we
// refuse any bundle with api_min > WEB_API_VERSION.
#define WEB_API_VERSION 1u

#define WEB_MANIFEST_MAX_ASSETS 32
#define WEB_MANIFEST_PATH_MAX   64
#define WEB_MANIFEST_SHA256_HEX 64

typedef struct {
    char   path[WEB_MANIFEST_PATH_MAX];
    char   sha256_hex[WEB_MANIFEST_SHA256_HEX + 1];
    size_t size;
} web_manifest_asset_t;

typedef struct {
    unsigned             api_min;
    size_t               n_assets;
    web_manifest_asset_t assets[WEB_MANIFEST_MAX_ASSETS];
} web_manifest_t;

// True if relpath is safe under the web root: no leading slash, no "..", no
// backslash, non-empty, fits WEB_MANIFEST_PATH_MAX.
bool web_manifest_path_ok(const char *relpath);

// True if s is exactly 64 lowercase hex digits.
bool web_manifest_sha256_hex_ok(const char *s);

// api_min must be >= 1 and <= device_api. Plain refuse for skew (RFC 0004 §4).
bool web_manifest_api_ok(unsigned api_min, unsigned device_api);

// Parse the constrained JSON shape documented in RFC 0004 §4. On failure
// writes a short reason into err (if non-NULL) and returns false. Does not
// allocate.
bool web_manifest_parse(const char *json, web_manifest_t *out, char *err, size_t err_len);

#ifdef __cplusplus
}
#endif
