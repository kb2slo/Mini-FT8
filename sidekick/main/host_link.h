#pragma once

// ============================================================================
// host_link.h — what the host is doing, received over PORTA and served to a
// browser (I28a, RFC 0004 §6).
//
// Read-only on purpose. This is the first slice that runs the whole stack for
// real -- host -> PORTA -> sidekick -> HTTP -> phone -- and it does so without
// needing the control direction, the pairing token, the app partition or the
// signing chain. None of those can block a working end-to-end path, and a
// viewer cannot key a transmitter.
//
// It also gives the link a free correctness oracle: the same lines appear on
// the ADV's own screen (`P` then `.`), so if the two disagree the fault is
// between them.
// ============================================================================

#include "esp_http_server.h"

// Starts the PORTA receive task. Independent of WiFi -- events are printed on
// this device's USB-C whether or not anything is serving them, because that
// console is the point when the ADV's own USB-C is busy hosting the radio.
void host_link_start(void);

// Adds the viewer and its polling endpoint to an already-started server.
void host_link_register_uris(httpd_handle_t server);
