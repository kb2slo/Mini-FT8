// Host test: bundle-host URL validation (I28c, RFC 0004 §4).
// Soft identity — https base only; no product name baked into the rule.

#include "bundle_host.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0;

static void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        g_fail++;
    }
}

int main()
{
    check(bundle_host_url_ok(BUNDLE_HOST_DEFAULT), "default URL ok");
    check(bundle_host_url_ok("https://example.com/app"), "plain https ok");
    check(!bundle_host_url_ok(nullptr), "null rejected");
    check(!bundle_host_url_ok(""), "empty rejected");
    check(!bundle_host_url_ok("http://example.com/app"), "http rejected");
    check(!bundle_host_url_ok("https://example.com/app/"), "trailing slash rejected");
    check(!bundle_host_url_ok("https://exam ple.com/app"), "space rejected");
    check(!bundle_host_url_ok("https://example.com/a\"b"), "quote rejected");

    char long_url[BUNDLE_HOST_MAX + 8];
    std::memset(long_url, 'a', sizeof(long_url));
    std::memcpy(long_url, "https://", 8);
    long_url[BUNDLE_HOST_MAX] = '\0';
    check(!bundle_host_url_ok(long_url), "overlong rejected");

    if (g_fail) {
        std::printf("%d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
