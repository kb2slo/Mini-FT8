// Host test for I28c bundle manifest parse + API version gate (RFC 0004 §4).
//
// Pins the refuse-incompatible rule and the path/digest jail that make a bad
// upload fail before promote, not after. Device install (stage/verify/promote)
// is not covered here — it needs LittleFS.

#include "web_manifest.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;

static void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        g_fail++;
    }
}

static const char *k_sha =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

static std::string one_asset(unsigned api_min, const char *path = "app.js", size_t size = 0)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "{\"api_min\":%u,\"assets\":[{\"path\":\"%s\",\"sha256\":\"%s\",\"size\":%u}]}",
                  api_min, path, k_sha, (unsigned)size);
    return std::string(buf);
}

static void test_path_ok()
{
    check(web_manifest_path_ok("app.js"), "plain file ok");
    check(web_manifest_path_ok("css/app.css"), "nested ok");
    check(!web_manifest_path_ok(""), "empty rejected");
    check(!web_manifest_path_ok("/abs"), "leading slash rejected");
    check(!web_manifest_path_ok("../x"), "dot-dot rejected");
    check(!web_manifest_path_ok("a/../b"), "embedded dot-dot rejected");
    check(!web_manifest_path_ok("a\\b"), "backslash rejected");
    check(!web_manifest_path_ok(".staging"), "dotfile rejected");
    check(!web_manifest_path_ok("x/.y"), "dot component rejected");
}

static void test_sha_ok()
{
    check(web_manifest_sha256_hex_ok(k_sha), "empty-file digest ok");
    check(!web_manifest_sha256_hex_ok("abc"), "short digest rejected");
    std::string upper(k_sha);
    for (char &c : upper) {
        if (c >= 'a' && c <= 'f') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    check(!web_manifest_sha256_hex_ok(upper.c_str()), "uppercase digest rejected");
}

static void test_api_ok()
{
    check(web_manifest_api_ok(1, WEB_API_VERSION), "api_min 1 accepted at device 1");
    check(web_manifest_api_ok(WEB_API_VERSION, WEB_API_VERSION), "equal versions accepted");
    check(!web_manifest_api_ok(WEB_API_VERSION + 1, WEB_API_VERSION),
          "api_min newer than device refused");
    check(!web_manifest_api_ok(0, WEB_API_VERSION), "api_min 0 refused");
}

static void test_parse_good()
{
    web_manifest_t m;
    char err[64];
    const std::string j = one_asset(1, "app.js", 12);
    check(web_manifest_parse(j.c_str(), &m, err, sizeof(err)), "minimal manifest parses");
    check(m.api_min == 1, "api_min stored");
    check(m.n_assets == 1, "one asset");
    check(std::strcmp(m.assets[0].path, "app.js") == 0, "path stored");
    check(std::strcmp(m.assets[0].sha256_hex, k_sha) == 0, "sha stored");
    check(m.assets[0].size == 12, "size stored");
}

static void test_parse_whitespace()
{
    web_manifest_t m;
    char err[64];
    const char *j =
        "{\n  \"api_min\" : 1 ,\n  \"assets\" : [ { \"path\" : \"x\" , "
        "\"sha256\" : \""
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        "\" , \"size\" : 0 } ]\n}\n";
    check(web_manifest_parse(j, &m, err, sizeof(err)), "whitespace tolerated");
}

static void test_parse_refuses()
{
    web_manifest_t m;
    char err[64];

    check(!web_manifest_parse("{}", &m, err, sizeof(err)), "empty object refused");
    check(!web_manifest_parse("{\"api_min\":1,\"assets\":[]}", &m, err, sizeof(err)),
          "empty assets refused");
    check(!web_manifest_parse(
              "{\"api_min\":1,\"assets\":[{\"path\":\"../x\",\"sha256\":"
              "\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\","
              "\"size\":0}]}",
              &m, err, sizeof(err)),
          "unsafe path in assets refused");
    check(!web_manifest_parse(
              "{\"api_min\":1,\"assets\":[{\"path\":\"a\",\"sha256\":"
              "\"E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855\","
              "\"size\":0}]}",
              &m, err, sizeof(err)),
          "uppercase sha in assets refused");

    // Duplicate path.
    char dup[640];
    std::snprintf(dup, sizeof(dup),
                  "{\"api_min\":1,\"assets\":["
                  "{\"path\":\"a\",\"sha256\":\"%s\",\"size\":0},"
                  "{\"path\":\"a\",\"sha256\":\"%s\",\"size\":1}"
                  "]}",
                  k_sha, k_sha);
    check(!web_manifest_parse(dup, &m, err, sizeof(err)), "duplicate path refused");

    check(!web_manifest_parse("{\"api_min\":1,\"assets\":[],\"extra\":1}", &m, err, sizeof(err)),
          "unknown top-level field refused");
}

static void test_parse_then_version_gate()
{
    // Parse must succeed for a future api_min so begin can say why it refused.
    web_manifest_t m;
    char err[64];
    const std::string j = one_asset(WEB_API_VERSION + 9);
    check(web_manifest_parse(j.c_str(), &m, err, sizeof(err)),
          "future api_min still parses");
    check(m.api_min == WEB_API_VERSION + 9, "future api_min stored");
    check(!web_manifest_api_ok(m.api_min, WEB_API_VERSION),
          "future api_min fails the version gate");
}

int main()
{
    test_path_ok();
    test_sha_ok();
    test_api_ok();
    test_parse_good();
    test_parse_whitespace();
    test_parse_refuses();
    test_parse_then_version_gate();
    if (g_fail) {
        std::printf("%d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
