#pragma once

// pairing_http.h -- the ESP-IDF half of RFC 0004 §7's pairing token.
//
// Split from pairing_token.{c,h} along the line that matters for testing: the
// decision logic there is dependency-free and host-tested, while everything
// that needs NVS, esp_http_server or a clock lives here and is verified on
// hardware. Nothing in this file makes an authorization decision of its own --
// it gathers the two inputs and calls pairing_allows() once.

#include "esp_err.h"
#include "esp_http_server.h"

#include "pairing_token.h"

// Loads the device token from NVS, minting and storing one if there is none.
// Call once before registering any route. Returns ESP_OK only when a
// well-formed token is in hand; on failure every guarded route refuses, which
// is the intended direction.
esp_err_t pairing_http_init(void);

// Registers `uri` on `server`, refusing requests that `policy` does not allow.
//
// **The policy argument is required, and that is the whole point of this
// function existing.** A path-keyed table would decide by asking whether
// someone remembered to think about a route, which is the question it would
// exist to stop asking. Here a new route does not compile until its author
// names a policy, and the answer sits beside the handler instead of in a
// table in another file.
//
// Guarded requests are refused with 401 before the handler runs, so a handler
// never has to remember to check -- and never sees a request it should not.
esp_err_t pairing_http_register(httpd_handle_t server, const httpd_uri_t *uri, pairing_policy_t policy);

// Registers the token disclosure route. Call from both the provisioning and
// station servers: the route exists in either mode, because RFC 0004 §1 makes
// AP mode an operating mode rather than a setup state, and a retrieval path
// that only worked during setup would be the assumption B55 warns about.
esp_err_t pairing_http_register_disclosure(httpd_handle_t server);

// Opens the disclosure window (RFC 0004 §7). Driven by the AtomS3 Lite's user
// button, which is physical and therefore correct in either mode -- gating
// disclosure on AP mode would stop being a gate the moment the application
// runs there. Re-arming while open extends the window rather than stacking.
void pairing_http_open_disclosure(void);
