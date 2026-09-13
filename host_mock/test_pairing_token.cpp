// Host test for the sidekick's API authorization (I28b, RFC 0004 §7).
//
// This module decides who may key a transmitter, so the things worth pinning
// are the ones that fail open rather than the ones that fail loudly. Three in
// particular: a guarded route with no token minted denies rather than
// admitting everyone; the token comparison does not short-circuit; and the
// form parser matches whole fields, so `nottoken=` and `token_extra=` cannot
// satisfy a `token=` requirement.
//
// There is deliberately no route table to test. Which routes are open is named
// where each route is registered, as a required argument, so the fail-closed
// property is a build error rather than a default this test would have to
// police. What is left here is the decision itself.

#include "pairing_token.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        g_fail++;
    }
}

static std::string format_of(const uint8_t* bytes)
{
    char buf[PAIRING_TOKEN_BUF];
    pairing_token_format(bytes, buf);
    return std::string(buf);
}

static void test_format()
{
    const uint8_t zeros[PAIRING_TOKEN_BYTES] = {};
    check(format_of(zeros) == std::string(PAIRING_TOKEN_LEN, '0'), "all-zero entropy renders as zeros");

    uint8_t ones[PAIRING_TOKEN_BYTES];
    std::memset(ones, 0xFF, sizeof(ones));
    check(format_of(ones) == std::string(PAIRING_TOKEN_LEN, 'f'), "all-ones entropy renders lowercase f");

    // Nibble order: high nibble first, so the token reads the same way the
    // bytes would be dumped. An inverted pair here would still round-trip
    // through equal() and only show up against an externally captured value.
    const uint8_t counting[PAIRING_TOKEN_BYTES] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
    };
    check(format_of(counting) == "0123456789abcdeffedcba9876543210", "hex is high nibble first");

    check(format_of(counting).size() == PAIRING_TOKEN_LEN, "token is exactly PAIRING_TOKEN_LEN");
}

static void test_well_formed()
{
    const std::string good(PAIRING_TOKEN_LEN, 'a');
    check(pairing_token_is_well_formed(good.c_str()), "32 lowercase hex is well formed");

    check(!pairing_token_is_well_formed(nullptr), "null is not well formed");
    check(!pairing_token_is_well_formed(""), "empty is not well formed");
    check(!pairing_token_is_well_formed(std::string(PAIRING_TOKEN_LEN - 1, 'a').c_str()),
          "one character short is rejected");
    check(!pairing_token_is_well_formed(std::string(PAIRING_TOKEN_LEN + 1, 'a').c_str()),
          "one character long is rejected -- a prefix must not pass");

    // Uppercase is rejected rather than folded. Only pairing_token_format()
    // ever mints one, so uppercase means a bug or an attacker, and accepting
    // two spellings invites a comparison that disagrees with itself.
    check(!pairing_token_is_well_formed(std::string(PAIRING_TOKEN_LEN, 'A').c_str()),
          "uppercase hex is rejected, not folded");

    std::string non_hex(PAIRING_TOKEN_LEN, 'a');
    non_hex[PAIRING_TOKEN_LEN - 1] = 'g';
    check(!pairing_token_is_well_formed(non_hex.c_str()), "non-hex letter is rejected");

    // An embedded NUL must not let a short token pass as a long one.
    std::string embedded_nul(PAIRING_TOKEN_LEN, 'a');
    embedded_nul[5] = '\0';
    check(!pairing_token_is_well_formed(embedded_nul.c_str()), "embedded NUL is rejected");
}

static void test_equal()
{
    const std::string a(PAIRING_TOKEN_LEN, 'a');
    const std::string b(PAIRING_TOKEN_LEN, 'b');
    check(pairing_token_equal(a.c_str(), a.c_str()), "identical tokens compare equal");
    check(!pairing_token_equal(a.c_str(), b.c_str()), "different tokens compare unequal");

    // A long shared prefix must not pass. This is the case a length-only or
    // prefix comparison would wave through.
    std::string almost = a;
    almost[PAIRING_TOKEN_LEN - 1] = 'b';
    check(!pairing_token_equal(a.c_str(), almost.c_str()), "differing final character compares unequal");
    std::string first_differs = a;
    first_differs[0] = 'b';
    check(!pairing_token_equal(a.c_str(), first_differs.c_str()), "differing first character compares unequal");

    check(!pairing_token_equal(nullptr, a.c_str()), "null never compares equal");
    check(!pairing_token_equal(a.c_str(), nullptr), "null never compares equal either way");
    check(!pairing_token_equal("", ""), "two empty strings are not a match");

    // An unset token in NVS reads back as empty, and a client sending an empty
    // header must not then be authorized. Both halves are ill-formed, so this
    // is already covered -- pinned because "no token configured" authorizing
    // everyone is the worst available failure and must never be reachable.
    check(!pairing_token_equal("", a.c_str()), "empty stored token authorizes nobody");
}

static void test_allows()
{
    const std::string stored(PAIRING_TOKEN_LEN, 'a');
    const std::string other(PAIRING_TOKEN_LEN, 'b');

    // Open routes check nothing, including when no token has ever been minted.
    // Reads of radio data are open on principle, so this must not depend on
    // pairing state at all.
    check(pairing_allows(PAIRING_OPEN, stored.c_str(), nullptr), "open route allows an unpaired caller");
    check(pairing_allows(PAIRING_OPEN, nullptr, nullptr), "open route allows with no token minted");
    check(pairing_allows(PAIRING_OPEN, "", ""), "open route allows with both sides empty");
    check(pairing_allows(PAIRING_OPEN, stored.c_str(), other.c_str()), "open route allows a wrong token");

    // Guarded routes.
    check(pairing_allows(PAIRING_REQUIRED, stored.c_str(), stored.c_str()), "matching token is allowed");
    check(!pairing_allows(PAIRING_REQUIRED, stored.c_str(), other.c_str()), "wrong token is refused");
    check(!pairing_allows(PAIRING_REQUIRED, stored.c_str(), nullptr), "absent token is refused");
    check(!pairing_allows(PAIRING_REQUIRED, stored.c_str(), ""), "empty token is refused");

    // The worst failure available here: an unminted token must not authorize
    // everyone, and must not be satisfiable by presenting the same emptiness.
    // NVS returns one of these two shapes when the key is missing, so both are
    // pinned rather than assumed equivalent.
    check(!pairing_allows(PAIRING_REQUIRED, nullptr, nullptr), "no token stored authorizes nobody");
    check(!pairing_allows(PAIRING_REQUIRED, "", ""), "unminted token is not matched by an empty header");
    check(!pairing_allows(PAIRING_REQUIRED, nullptr, stored.c_str()), "no token stored refuses even a real one");
    check(!pairing_allows(PAIRING_REQUIRED, "", stored.c_str()), "empty stored token refuses even a real one");

    // A malformed stored token -- truncated in NVS, hand-edited -- denies
    // rather than being matched by the same malformed value.
    const std::string malformed(PAIRING_TOKEN_LEN - 1, 'a');
    check(!pairing_allows(PAIRING_REQUIRED, malformed.c_str(), malformed.c_str()),
          "a malformed stored token cannot be matched, even by itself");

    // Near-miss, since this is the case a prefix or length comparison passes.
    std::string almost = stored;
    almost[PAIRING_TOKEN_LEN - 1] = 'b';
    check(!pairing_allows(PAIRING_REQUIRED, stored.c_str(), almost.c_str()),
          "a token differing in one character is refused");
}

static void test_from_header()
{
    const std::string tok(PAIRING_TOKEN_LEN, 'c');
    char out[PAIRING_TOKEN_BUF];

    check(pairing_token_from_header(tok.c_str(), out) && tok == out, "bare token parses");

    const std::string padded = "  \t" + tok;
    check(pairing_token_from_header(padded.c_str(), out) && tok == out, "leading whitespace is skipped");

    check(!pairing_token_from_header(nullptr, out), "null header value is refused");
    check(!pairing_token_from_header("", out), "empty header value is refused");
    check(!pairing_token_from_header(("Bearer " + tok).c_str(), out),
          "no Bearer prefix is accepted -- one shape only");
    check(!pairing_token_from_header((tok + " ").c_str(), out), "trailing whitespace is refused");
    check(!pairing_token_from_header((tok + "d").c_str(), out), "an over-long value is refused");
}

int main()
{
    test_format();
    test_well_formed();
    test_equal();
    test_allows();
    test_from_header();

    if (g_fail == 0) {
        std::printf("pairing_token: all checks passed\n");
    }
    return g_fail == 0 ? 0 : 1;
}
