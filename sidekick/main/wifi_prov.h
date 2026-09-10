#pragma once

// ============================================================================
// wifi_prov.h — WiFi bring-up and SoftAP credential provisioning.
//
// RFC 0001 §5.2d: the sidekick pulls its own firmware over HTTPS rather than
// being pushed to over the wire, which means it has to get onto a network
// first. A factory part has no credentials and the ADV cannot supply them
// (that would need Station.txt fields and menu space that does not exist), so
// the sidekick provisions itself through the browser UI it has to serve
// anyway.
//
// Boot sequence:
//   1. credentials in NVS -> try to join, retry a few times
//   2. no credentials, or the join fails -> raise an open SoftAP and serve a
//      one-page form on http://192.168.4.1/
//   3. form submitted -> store to NVS, try to join, report the result
//
// The PORTA beacon is independent of all of this and keeps running throughout
// (different peripheral, different task) — a sidekick with no WiFi is still a
// working companion, just not a self-updating one.
// ============================================================================

#include <stdbool.h>

// Brings up NVS, netif, the event loop and WiFi, then runs the sequence above.
// Returns once the outcome is settled: either joined, or the AP is up and
// serving. Never blocks forever — a failed join falls back to the AP rather
// than retrying indefinitely.
void wifi_prov_start(void);

// True once an IP has been obtained on the station interface.
bool wifi_prov_is_online(void);

// SSID currently joined, or "" when offline. Points at static storage.
const char* wifi_prov_ssid(void);

// True while the provisioning AP is up.
bool wifi_prov_ap_active(void);

// Hostname the sidekick answers to over mDNS, in both AP and station mode:
// http://minift8.local/ . Without it the operator has to read an IP off a
// serial console -- which is precisely the console the finished product will
// not have attached.
#define WIFI_PROV_MDNS_HOST "minift8"

// Forget the stored network and restart into provisioning. Does not return.
//
// Credentials live in exactly one place -- this module's NVS keys -- because
// wifi_prov_start() puts the driver in WIFI_STORAGE_RAM. Without that the
// driver keeps its own copy in nvs.net80211, and a "forget" clearing only our
// keys would leave the device reconnecting to the network it was told to
// forget.
void wifi_prov_forget_and_restart(void);
