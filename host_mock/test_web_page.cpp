// Host test for the bootstrap page templating (web_page.{c,h}).
//
// Two things here are worth more than the templating itself. The first is that
// values escape by default: the status page used to interpolate the stored
// SSID raw, and an SSID is whatever a nearby access point broadcast, so a
// network named with a `<script>` tag executed on the origin that now holds
// the pairing token. The second is that the expansion is chunked -- it emits
// runs rather than building the page in a buffer -- so the boundaries between
// literal text, escaped values and placeholders are where an off-by-one would
// live, and the tests reassemble the chunks to catch one.

#include "web_page.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        g_fail++;
    }
}

struct Sink {
    std::string out;
    int         chunks    = 0;
    int         fail_after = -1;  // abort the nth emit, as a dropped socket would
};

static bool sink_emit(void* ctx, const char* data, size_t len)
{
    Sink* s = static_cast<Sink*>(ctx);
    if (s->fail_after >= 0 && s->chunks >= s->fail_after) {
        return false;
    }
    s->chunks++;
    s->out.append(data, len);
    return true;
}

static std::string expand(const std::string& page, const std::vector<web_sub_t>& subs, bool* ok = nullptr)
{
    Sink s;
    const bool r = web_page_expand(page.c_str(), subs.empty() ? nullptr : subs.data(), subs.size(),
                                   sink_emit, &s);
    if (ok) {
        *ok = r;
    }
    return s.out;
}

static std::string escape_of(const char* in, size_t cap = 256)
{
    std::vector<char> buf(cap);
    if (!web_html_escape(in, buf.data(), buf.size())) {
        return "<<too small>>";
    }
    return std::string(buf.data());
}

static void test_escape()
{
    check(escape_of("plain text") == "plain text", "plain text passes through");
    check(escape_of("") == "", "empty string escapes to empty");
    check(escape_of(nullptr) == "", "null input escapes to empty, not a crash");

    check(escape_of("<script>") == "&lt;script&gt;", "angle brackets escape");
    check(escape_of("a&b") == "a&amp;b", "ampersand escapes");
    check(escape_of("say \"hi\"") == "say &quot;hi&quot;", "double quote escapes -- attribute break");
    check(escape_of("it's") == "it&#39;s", "single quote escapes numerically");

    // The ampersand must be escaped as itself, not re-escaped from an entity
    // it produced, or output doubles on every pass.
    check(escape_of("&amp;") == "&amp;amp;", "an existing entity is escaped once, not interpreted");

    // The attack this exists to stop, in the shape it would actually arrive:
    // an access point name, chosen from a scan list.
    check(escape_of("<img src=x onerror=alert(1)>") == "&lt;img src=x onerror=alert(1)&gt;",
          "a hostile SSID cannot open a tag");

    // Too small leaves the buffer empty rather than truncated. A truncation
    // could cut an entity in half, or cut off an attribute's closing quote and
    // swallow the rest of the tag.
    char tiny[4];
    check(!web_html_escape("<<<<", tiny, sizeof(tiny)), "overflow is refused");
    check(tiny[0] == '\0', "a refused escape leaves an empty string, never a partial entity");

    // Exact fit, both ways: "&lt;" is 4 bytes plus a terminator.
    char exact[5];
    check(web_html_escape("<", exact, sizeof(exact)) && std::string(exact) == "&lt;", "exact fit succeeds");
    char one_short[4];
    check(!web_html_escape("<", one_short, sizeof(one_short)), "one byte short is refused");
}

static void test_expand()
{
    check(expand("<p>hello</p>", {}) == "<p>hello</p>", "a page with no placeholders is unchanged");
    check(expand("", {}) == "", "empty page");

    const std::vector<web_sub_t> ssid = {{"ssid", "HomeNet", false}};
    check(expand("<dd>{{ssid}}</dd>", ssid) == "<dd>HomeNet</dd>", "one placeholder substitutes");
    check(expand("{{ssid}}", ssid) == "HomeNet", "a placeholder alone is the whole page");
    check(expand("{{ssid}}{{ssid}}", ssid) == "HomeNetHomeNet", "the same key twice");

    const std::vector<web_sub_t> two = {{"a", "1", false}, {"b", "2", false}};
    check(expand("{{a}}-{{b}}", two) == "1-2", "adjacent placeholders");
    check(expand("x{{b}}y{{a}}z", two) == "x2y1z", "order follows the page, not the table");

    // Escaping runs through substitution, which is the whole point.
    const std::vector<web_sub_t> hostile = {{"ssid", "<script>evil()</script>", false}};
    check(expand("<dd>{{ssid}}</dd>", hostile) == "<dd>&lt;script&gt;evil()&lt;/script&gt;</dd>",
          "a substituted value cannot inject a tag");

    // ... unless it is explicitly pre-built HTML, which the scan list is.
    const std::vector<web_sub_t> prebuilt = {{"networks", "<option>A</option>", true}};
    check(expand("<select>{{networks}}</select>", prebuilt) == "<select><option>A</option></select>",
          "already_html passes markup through");

    // A NULL value is empty, not a crash and not the literal placeholder.
    const std::vector<web_sub_t> nulled = {{"why", nullptr, false}};
    check(expand("<p>{{why}}</p>", nulled) == "<p></p>", "a null value renders empty");
    const std::vector<web_sub_t> empty_val = {{"why", "", false}};
    check(expand("<p>{{why}}</p>", empty_val) == "<p></p>", "an empty value renders empty");

    // Unknown and malformed placeholders pass through verbatim, so a typo is
    // visible on the page rather than an empty field, and the raw file in a
    // browser looks like the served one.
    check(expand("<dd>{{ssdi}}</dd>", ssid) == "<dd>{{ssdi}}</dd>", "an unknown key is left alone");
    check(expand("{{ssid", ssid) == "{{ssid", "an unterminated placeholder is not swallowed");
    check(expand("a{{b", two) == "a{{b", "no closing braces keeps the tail");
    check(expand("{{}}", ssid) == "{{}}", "an empty key matches nothing");
    check(expand("{ {ssid}}", ssid) == "{ {ssid}}", "a single brace is not a placeholder");
    check(expand("{{{ssid}}", ssid) == "{HomeNet", "the innermost braces win");

    // A key that is a prefix of another must not match it, and vice versa --
    // the lookup compares whole keys, not prefixes.
    const std::vector<web_sub_t> prefixes = {{"ip", "10.0.0.1", false}, {"ipv6", "::1", false}};
    check(expand("{{ip}}|{{ipv6}}", prefixes) == "10.0.0.1|::1", "a key that prefixes another matches exactly");

    // CSS with a percent sign, which is the thing that forced `%%` when these
    // pages lived in C and is now just a character.
    check(expand("<style>button{width:100%}</style>", {}) == "<style>button{width:100%}</style>",
          "a percent sign needs no escaping now");
}

static void test_abort()
{
    // A dropped connection mid-page stops the expansion instead of running to
    // the end writing into a closed socket.
    Sink s;
    s.fail_after = 1;
    const web_sub_t subs[] = {{"ssid", "HomeNet", false}};
    const bool ok = web_page_expand("<dd>{{ssid}}</dd>", subs, 1, sink_emit, &s);
    check(!ok, "a failed emit aborts the expansion");
    check(s.chunks == 1, "no further chunks are written after a failure");

    check(!web_page_expand(nullptr, nullptr, 0, sink_emit, &s), "a null page is refused");
    check(!web_page_expand("x", nullptr, 0, nullptr, nullptr), "a null emit is refused");
}

int main()
{
    test_escape();
    test_expand();
    test_abort();

    if (g_fail == 0) {
        std::printf("web_page: all checks passed\n");
    }
    return g_fail == 0 ? 0 : 1;
}
