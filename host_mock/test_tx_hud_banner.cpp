#include <cstdio>
#include <cstring>

#include "tx_hud_banner.h"

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

static void expect_str(const char* got, const char* want, const char* msg)
{
    if (std::strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL: %s (got '%s' want '%s')\n", msg, got, want);
        ++g_fails;
    }
}

int main()
{
    expect_int(tx_hud_banner_list_rows(false, false, 6), 6, "idle six");
    expect_int(tx_hud_banner_list_rows(false, true, 6), 5, "sticky five");
    expect_int(tx_hud_banner_list_rows(true, true, 6), 5, "banner wins sticky");
    expect_int(tx_hud_banner_list_rows(true, false, 6), 5, "banner five");

    expect_true(tx_hud_banner_line_is_list(0, 5), "key1 list");
    expect_true(tx_hud_banner_line_is_list(4, 5), "key5 list");
    expect_true(!tx_hud_banner_line_is_list(5, 5), "key6 banner");

    char msg[kTxHudBannerCols + 1];
    char status[kTxHudBannerCols + 1];
    TxHudBannerInput in;

    in.tx_text = "CQ KB2SLO FN30";
    in.tx_aborted = true;
    tx_hud_banner_format(in, msg, status, (int)sizeof(msg));
    expect_str(msg, "CQ KB2SLO FN30", "abort keeps tx text");
    expect_str(status, "TX ABORT (low batt)", "abort status");

    in = {};
    in.tx_text = "KB2SLO W1AW R-10 extra-long";
    tx_hud_banner_format(in, msg, status, (int)sizeof(msg));
    expect_int((int)std::strlen(msg), kTxHudBannerCols, "msg truncates to 20");
    expect_str(status, "--", "empty meters");

    in = {};
    in.tx_text = "CQ KB2SLO FN30";
    in.power_w = 3.2f;
    in.swr = 1.20f;
    in.voltage_mv = 3720;
    in.percent = 87;
    tx_hud_banner_format(in, msg, status, (int)sizeof(msg));
    expect_str(msg, "CQ KB2SLO FN30", "qmx msg");
    expect_str(status, "3.2W 1.20 87%", "packed rf+percent");

    in.writes_blocked = true;
    tx_hud_banner_format(in, msg, status, (int)sizeof(msg));
    expect_str(status, "87% WR BLOCK", "writes blocked");

    in = {};
    in.voltage_mv = 3720;
    tx_hud_banner_format(in, msg, status, (int)sizeof(msg));
    expect_str(status, "3720mV", "mv only if no percent");

    in = {};
    in.tx_text = "CQ KB2SLO FN30";
    in.power_w = 3.2f;
    in.swr = 1.20f;
    in.percent = 87;
    expect_true(!tx_hud_banner_show_status(in, 0), "t0 message");
    expect_true(!tx_hud_banner_show_status(in, 1499), "before swap message");
    expect_true(tx_hud_banner_show_status(in, 1500), "swap to status");
    in.tx_aborted = true;
    expect_true(tx_hud_banner_show_status(in, 0), "abort freezes status");
    in.tx_aborted = false;
    in.swr = 2.00f;
    expect_true(tx_hud_banner_show_status(in, 0), "swr warn freezes status");
    in.swr = 1.20f;
    in.writes_blocked = true;
    expect_true(tx_hud_banner_show_status(in, 0), "writes block freezes status");

    if (g_fails != 0) {
        fprintf(stderr, "%d FAIL(s)\n", g_fails);
        return 1;
    }
    printf("PASS: tx hud banner\n");
    return 0;
}
