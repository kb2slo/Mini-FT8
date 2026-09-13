#pragma once

// pairing_token.h -- authorization for the sidekick's HTTP API (I28b, RFC 0004 §7).
//
// RFC 0004 §4 rejected image signing: the bundle is already authenticated by
// the browser's TLS session to the bundle host, one hop before the device sees
// it. What the device actually lacks is a way to tell an operator's paired
// browser from any other device on the LAN. This is that, and it is therefore
// the whole of the security design rather than a layer on top of one.
//
// **Reads are open on principle, not as a concession.** Anyone may listen to
// amateur transmissions on licensed spectrum, and Part 97 forbids obscuring
// the meaning of a transmission in the first place, so there is nothing to
// protect in a decode. That principle carries to the LAN: decodes, status and
// the log are open to anyone who can reach the device. What needs
// authorization is *changing* the device -- keying the transmitter, writing
// config, taking an image -- because that is what the licensee answers for.
//
// The one carve-out, because the principle does not reach it: a credential is
// not radio data. A read that would hand back the WiFi password or this token
// stays guarded. That carve-out is why a route's policy is named where the
// route is registered rather than derived from its HTTP method -- "GET is
// open" is right for every route today and silently wrong for the first one
// that returns a secret, and a method rule leaves nowhere to say so.
//
// Deliberately dependency-free -- no ESP-IDF, no esp_http_server, no crypto
// library -- so host_mock compiles this exact file rather than a second copy
// of a policy that decides who may key a transmitter. It stays a plain pair in
// sidekick/main/ rather than a component: only one idf.py project compiles it,
// and STYLE is explicit that host-testability alone does not earn a
// components/ directory. Entropy is injected rather than drawn here, which is
// what makes minting host-testable.
//
// Scope, from RFC 0004 §7: this is a nuisance control, not a targeted-attacker
// control. The token travels in clear over plain HTTP and is replayable by
// anyone who can read the traffic. WPA2/WPA3 per-station encryption makes that
// materially harder; an open network does not. Raising that bar needs a
// certificate for a name that resolves on one LAN, which §3 declined.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 128 bits, rendered lowercase hex. Long enough that guessing is not a threat
// on a LAN, short enough to read off a screen and retype once.
#define PAIRING_TOKEN_BYTES 16u
#define PAIRING_TOKEN_LEN   (PAIRING_TOKEN_BYTES * 2u)
#define PAIRING_TOKEN_BUF   (PAIRING_TOKEN_LEN + 1u)

// The header the web app sends. A header rather than a cookie on purpose:
// an ambient cookie would make every write endpoint CSRF-able by any page the
// operator happens to visit, since a form POST of urlencoded data needs no
// preflight and would carry the cookie automatically. An explicit token the
// attacker cannot read closes that, and costs nothing here because requests
// from the app are same-origin -- the simple-request restriction in §4 applies
// to the cross-origin bundle fetch, not to this.
// A header is also the *only* place the token may travel, which is a
// constraint rather than a preference. Checking a `token=` form field would
// mean reading the request body before the handler does, and esp_http_server
// has no rewind -- httpd_req_recv() consumes from the socket, so the wrapper
// would steal the body from the handler that needs it. Every guarded write is
// issued by JavaScript the sidekick itself serves, which can set a header, so
// a plain HTML form never needs to carry a token. The open provisioning form
// is the one that stays a plain form, and it needs no token at all.
#define PAIRING_TOKEN_HEADER "X-MiniFT8-Token"

// Renders `PAIRING_TOKEN_BYTES` of caller-supplied entropy as a NUL-terminated
// lowercase hex token. `out` must hold PAIRING_TOKEN_BUF bytes.
//
// The caller owns the entropy so that this is testable with known input; the
// sidekick passes esp_fill_random(). Do not substitute rand().
void pairing_token_format(const uint8_t *bytes, char *out);

// True only for exactly PAIRING_TOKEN_LEN lowercase hex characters and a NUL.
// Uppercase is rejected rather than folded: the token is only ever produced by
// pairing_token_format(), so anything else is a bug or an attacker, and
// accepting two spellings of one token invites a comparison that disagrees
// with itself.
bool pairing_token_is_well_formed(const char *s);

// Constant-time equality over the full token length. False unless both sides
// are well-formed and equal. Does not early-exit on the first differing byte,
// so response timing does not leak a prefix.
bool pairing_token_equal(const char *a, const char *b);

typedef enum {
    PAIRING_OPEN,      // served to anyone -- reads of radio data
    PAIRING_REQUIRED,  // refused without a valid token -- anything that changes the device
} pairing_policy_t;

// The authorization decision, and the only place it is made.
//
// `stored` is the device's token, as read back from NVS -- NULL or empty when
// none has been minted yet. `presented` is what the request carried, NULL when
// it carried none.
//
// PAIRING_REQUIRED with nothing stored denies. "Not yet configured" must never
// mean "open to everyone", which is the single worst failure available here
// and the reason this is one function with a test rather than a condition
// spelled out at each call site.
bool pairing_allows(pairing_policy_t policy, const char *stored, const char *presented);

// Copies the token out of a `PAIRING_TOKEN_HEADER` value. False if the value is
// absent or not a well-formed token. `out` must hold PAIRING_TOKEN_BUF bytes
// and is always NUL-terminated on success.
bool pairing_token_from_header(const char *header_value, char *out);

#ifdef __cplusplus
}
#endif
