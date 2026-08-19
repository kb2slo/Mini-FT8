#include <cstdio>
#include <string>
#include <vector>

#include "qso_browse.h"

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

static void expect_str(const std::string& got, const char* want, const char* msg)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got '%s' want '%s')\n", msg, got.c_str(), want);
        ++g_fails;
    }
}

int main()
{
    expect_true(qso_browse_is_daily_log_name("20260819.adi"), "daily adi");
    expect_true(qso_browse_is_daily_log_name("20260819.ADI"), "daily adi upper");
    expect_true(qso_browse_is_daily_log_name("20260819.txt"), "legacy txt");
    expect_true(!qso_browse_is_daily_log_name("Station.txt"), "station");
    expect_true(!qso_browse_is_daily_log_name("RT260819.txt"), "rt log");
    expect_true(!qso_browse_is_daily_log_name("2026081.adi"), "short date");

    std::vector<std::string> daily;
    qso_browse_select_daily_files(
        {"Station.txt", "20260818.adi", "20260819.adi", "notes.txt", "20260817.txt"},
        &daily);
    expect_int(static_cast<int>(daily.size()), 3, "three daily");
    expect_str(daily[0], "20260819.adi", "newest first");
    expect_str(daily[2], "20260817.txt", "legacy last");

    std::vector<std::string> lines;
    qso_browse_fill_file_lines({}, &lines);
    expect_int(static_cast<int>(lines.size()), 1, "empty files one line");
    expect_str(lines[0], "No YYYYMMDD.adi", "empty files message");
    qso_browse_fill_file_lines(daily, &lines);
    expect_str(lines[0], "20260819.adi", "file line");

    const QsoBrowseBand bands[] = {{"20m", 14074.0f}, {"40m", 7074.0f}};
    const std::string rec =
        "<call:5>K1ABC <gridsquare:4>FN42 <mode:3>FT8<qso_date:8>20260819 "
        "<time_on:6>153000 <freq:6>14.074 <station_callsign:6>KB2SLO "
        "<rst_sent:3>-10 <rst_rcvd:2>-8 <eor>";

    QsoBrowsePager pager;
    qso_browse_pager_reset(&pager, 0, 6);
    expect_true(qso_browse_pager_feed(&pager, "ADIF EXPORT", bands, 2), "header skip");
    expect_true(qso_browse_pager_feed(&pager, rec, bands, 2), "first qso");
    expect_int(static_cast<int>(pager.entries.size()), 1, "one entry");
    expect_str(pager.entries[0].call, "K1ABC", "call");
    expect_str(pager.entries[0].time_on, "15:30", "time hh:mm");
    expect_str(pager.entries[0].band, "20m", "band from freq");
    expect_true(pager.entries[0].has_rst_sent && pager.entries[0].rst_sent == -10, "rst sent");
    expect_true(pager.entries[0].has_rst_rcvd && pager.entries[0].rst_rcvd == -8, "rst rcvd");
    expect_true(!pager.has_next, "no next on one qso");

    qso_browse_format_entry_lines(pager.entries, QsoBrowsePageView::Default, &lines);
    expect_true(lines[0].find("15:30") != std::string::npos, "default has time");
    expect_true(lines[0].find("20m") != std::string::npos, "default has band");
    qso_browse_format_entry_lines(pager.entries, QsoBrowsePageView::Alternate, &lines);
    expect_true(lines[0].find("K1ABC") != std::string::npos, "alt has call");
    expect_true(lines[0].find("S-10") != std::string::npos, "alt has sent");

    QsoBrowsePager page;
    qso_browse_pager_reset(&page, 0, 6);
    for (int i = 0; i < 7; ++i) {
        const bool keep = qso_browse_pager_feed(&page, rec, bands, 2);
        if (i < 6) {
            expect_true(keep, "keep through sixth");
        } else {
            expect_true(!keep, "stop on seventh");
        }
    }
    expect_int(static_cast<int>(page.entries.size()), 6, "page size 6");
    expect_true(page.has_next, "seventh sets next");

    QsoBrowsePager page1;
    qso_browse_pager_reset(&page1, 6, 6);
    for (int i = 0; i < 7; ++i) {
        qso_browse_pager_feed(&page1, rec, bands, 2);
    }
    expect_int(static_cast<int>(page1.entries.size()), 1, "page 1 has the leftover");
    expect_true(!page1.has_next, "page 1 no further");

    qso_browse_format_entry_lines({}, QsoBrowsePageView::Default, &lines);
    expect_str(lines[0], "No QSOs", "empty entries");

    if (g_fails != 0) {
        fprintf(stderr, "%d failure(s)\n", g_fails);
        return 1;
    }
    printf("PASS: qso browse\n");
    return 0;
}
