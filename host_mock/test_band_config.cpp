#include <cstdio>
#include <cstring>

#include "band_config.h"

static int g_fails = 0;

static void fail(const char* msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    ++g_fails;
}

static void expect_true(bool cond, const char* msg)
{
    if (!cond) {
        fail(msg);
    }
}

static void expect_int(int got, int want, const char* msg)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %d want %d)\n", msg, got, want);
        ++g_fails;
    }
}

static void expect_event(BandConfigEvent got, BandConfigEvent want, const char* msg)
{
    expect_int(static_cast<int>(got), static_cast<int>(want), msg);
}

int main()
{
    // The 12 product bands, in g_bands order.
    const int numbers[] = {160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 6, 2};
    const int count = 12;

    band_config_reset(count);
    expect_int(band_config_page(), 0, "page 0");
    expect_int(band_config_focus(), 0, "focus 0");

    expect_event(band_config_handle_key('1'), BandConfigEvent::Toggle, "row 1 toggles");
    expect_int(band_config_focus(), 0, "focus 160m");
    expect_event(band_config_handle_key('6'), BandConfigEvent::Toggle, "row 6 toggles");
    expect_int(band_config_focus(), 5, "focus 20m");

    // Page 2 holds 17m..2m; row 1 there is index 6.
    expect_event(band_config_handle_key('.'), BandConfigEvent::PageChanged, "page down");
    expect_int(band_config_page(), 1, "page 1");
    expect_event(band_config_handle_key('1'), BandConfigEvent::Toggle, "page 2 row 1");
    expect_int(band_config_focus(), 6, "focus 17m");
    expect_event(band_config_handle_key('.'), BandConfigEvent::None, "no page 3");
    expect_int(band_config_page(), 1, "still page 1");
    expect_event(band_config_handle_key(';'), BandConfigEvent::PageChanged, "page up");
    expect_int(band_config_page(), 0, "back page 0");
    expect_event(band_config_handle_key(';'), BandConfigEvent::None, "no page -1");

    expect_event(band_config_handle_key('\n'), BandConfigEvent::EditFrequency, "enter edits");
    expect_event(band_config_handle_key('`'), BandConfigEvent::Exit, "backtick exits");
    expect_event(band_config_handle_key('x'), BandConfigEvent::None, "other key");

    // Rows past the band count do nothing (last page is not full).
    band_config_reset(8);
    band_config_handle_key('.');
    expect_int(band_config_page(), 1, "8 bands: page 1");
    expect_event(band_config_handle_key('2'), BandConfigEvent::Toggle, "index 7 ok");
    expect_int(band_config_focus(), 7, "focus 7");
    expect_event(band_config_handle_key('3'), BandConfigEvent::None, "index 8 out of range");
    expect_int(band_config_focus(), 7, "focus unchanged");

    // Saved text -> enable set.
    bool enabled[12] = {};
    band_config_from_text("80 40 20 17 15 12 10", numbers, count, enabled);
    expect_int(band_config_enabled_count(enabled, count), 7, "7 enabled");
    expect_true(!enabled[0] && enabled[1] && !enabled[2] && enabled[3], "160 off, 80 on");
    expect_true(enabled[5] && enabled[6] && enabled[9] && !enabled[10], "20/17/10 on, 6 off");

    // Round trip keeps g_bands order, not the order typed.
    char text[64];
    expect_int(band_config_to_text(enabled, numbers, count, text, sizeof(text)), 7, "7 tokens");
    expect_true(std::strcmp(text, "80 40 20 17 15 12 10") == 0, "serialize");

    band_config_from_text("20 40 80", numbers, count, enabled);
    band_config_to_text(enabled, numbers, count, text, sizeof(text));
    expect_true(std::strcmp(text, "80 40 20") == 0, "normalized order");

    // Empty or unmatched text means every band is in the rotation.
    band_config_from_text("", numbers, count, enabled);
    expect_int(band_config_enabled_count(enabled, count), count, "empty means all");
    band_config_from_text("11 13", numbers, count, enabled);
    expect_int(band_config_enabled_count(enabled, count), count, "no match means all");

    // Toggle on, toggle off, never empty.
    band_config_from_text("40", numbers, count, enabled);
    expect_int(band_config_enabled_count(enabled, count), 1, "one enabled");
    expect_true(!band_config_toggle(enabled, count, 3), "cannot drop last");
    expect_true(enabled[3], "40 still on");
    expect_true(band_config_toggle(enabled, count, 5), "enable 20");
    expect_true(enabled[5], "20 on");
    expect_true(band_config_toggle(enabled, count, 3), "now 40 can drop");
    expect_true(!enabled[3], "40 off");
    expect_int(band_config_enabled_count(enabled, count), 1, "back to one");

    expect_true(!band_config_toggle(enabled, count, -1), "negative index");
    expect_true(!band_config_toggle(enabled, count, count), "index past end");
    expect_true(!band_config_toggle(nullptr, count, 0), "null enabled");

    // Serializing into a short buffer stops on a whole token.
    band_config_from_text("", numbers, count, enabled);
    char small[10];
    const int wrote = band_config_to_text(enabled, numbers, count, small, sizeof(small));
    expect_true(wrote > 0 && wrote < count, "short buffer truncates");
    expect_true(std::strlen(small) < sizeof(small), "short buffer terminated");

    if (g_fails != 0) {
        fprintf(stderr, "%d FAIL(s)\n", g_fails);
        return 1;
    }
    printf("PASS: band_config\n");
    return 0;
}
