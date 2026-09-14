#pragma once

// web_bundle.h -- token-guarded stage → verify → promote (RFC 0004 §4).
//
// Wire protocol (all PAIRING_REQUIRED):
//   POST /api/bundle/begin   body = manifest JSON
//   PUT  /api/bundle/file?path=<relpath>  body = raw bytes
//   POST /api/bundle/commit
//
// begin refuses api_min > WEB_API_VERSION. commit promotes only after every
// asset is present under /.staging with a matching size and SHA-256.

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_bundle_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
