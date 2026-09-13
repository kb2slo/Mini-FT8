// Authorization for the sidekick's HTTP API (I28b, RFC 0004 §7). See
// pairing_token.h for why this is the whole security design, why reads are
// open on principle, and why it carries no dependencies.

#include <string.h>

#include "pairing_token.h"

static char hex_digit(uint8_t nibble)
{
    return (char)(nibble < 10u ? ('0' + nibble) : ('a' + (nibble - 10u)));
}

void pairing_token_format(const uint8_t *bytes, char *out)
{
    if (!bytes || !out) {
        return;
    }
    for (size_t i = 0; i < PAIRING_TOKEN_BYTES; ++i) {
        out[i * 2u]      = hex_digit((uint8_t)(bytes[i] >> 4));
        out[i * 2u + 1u] = hex_digit((uint8_t)(bytes[i] & 0x0Fu));
    }
    out[PAIRING_TOKEN_LEN] = '\0';
}

static bool is_lower_hex(char c)
{
    switch (c) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
            return true;
        default:
            return false;
    }
}

bool pairing_token_is_well_formed(const char *s)
{
    if (!s) {
        return false;
    }
    size_t i = 0;
    for (; i < PAIRING_TOKEN_LEN; ++i) {
        if (!is_lower_hex(s[i])) {
            return false;
        }
    }
    return s[i] == '\0';
}

bool pairing_token_equal(const char *a, const char *b)
{
    // Well-formedness is checked first and short-circuits, which leaks only
    // "that was not a token" -- a fact the caller already has from the length.
    // The comparison itself, where a prefix match would be worth learning, does
    // not short-circuit.
    if (!pairing_token_is_well_formed(a) || !pairing_token_is_well_formed(b)) {
        return false;
    }
    unsigned char diff = 0;
    for (size_t i = 0; i < PAIRING_TOKEN_LEN; ++i) {
        diff |= (unsigned char)((unsigned char)a[i] ^ (unsigned char)b[i]);
    }
    return diff == 0;
}

bool pairing_allows(pairing_policy_t policy, const char *stored, const char *presented)
{
    switch (policy) {
        case PAIRING_OPEN:
            // Reads of radio data, and the bootstrap pages. Nothing to check:
            // see the header on why listening is open on principle.
            return true;
        case PAIRING_REQUIRED:
            // pairing_token_equal() rejects an ill-formed side, so an unminted
            // token -- NULL or "" from NVS -- denies here rather than matching
            // an equally empty header.
            return pairing_token_equal(stored, presented);
    }
    return false;
}

bool pairing_token_from_header(const char *header_value, char *out)
{
    if (!header_value || !out) {
        return false;
    }
    // Leading optional whitespace only. No `Bearer ` prefix to strip: this is
    // a bespoke header, so accepting two shapes would be two code paths for no
    // caller's benefit.
    while (*header_value == ' ' || *header_value == '\t') {
        ++header_value;
    }
    if (!pairing_token_is_well_formed(header_value)) {
        return false;
    }
    memcpy(out, header_value, PAIRING_TOKEN_LEN);
    out[PAIRING_TOKEN_LEN] = '\0';
    return true;
}
