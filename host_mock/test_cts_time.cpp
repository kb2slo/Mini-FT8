#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/time.h>

#include "cts_time.h"

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

static void expect_long(long got, long want, const char* msg)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %ld want %ld)\n", msg, got, want);
        ++g_fails;
    }
}

int main()
{
    // 2024-01-01 00:00:00.000 UTC, Monday. Epoch 1704067200.
    const std::uint8_t k_new_year[] = {0xE8, 0x07, 1, 1, 0, 0, 0, 1, 0, 0};
    CtsCurrentTime ct = {};
    expect_true(cts_parse_current_time(k_new_year, sizeof(k_new_year), &ct), "parse 2024-01-01");
    expect_int(ct.year, 2024, "year");
    expect_int(ct.month, 1, "month");
    expect_int(ct.day, 1, "day");
    expect_int(ct.hour, 0, "hour");
    expect_int(ct.minute, 0, "minute");
    expect_int(ct.second, 0, "second");
    expect_int(ct.fractions256, 0, "frac0");

    struct timeval tv = {};
    expect_true(cts_parse_current_time_to_timeval(k_new_year, sizeof(k_new_year), &tv), "tv 2024");
    expect_long(static_cast<long>(tv.tv_sec), 1704067200L, "epoch 2024-01-01");
    expect_long(static_cast<long>(tv.tv_usec), 0L, "usec 0");

    // Fractions256 = 128 is exactly 0.5 s.
    std::uint8_t half[kCtsCurrentTimeSize];
    std::memcpy(half, k_new_year, sizeof(half));
    half[8] = 128;
    expect_true(cts_parse_current_time_to_timeval(half, sizeof(half), &tv), "tv half");
    expect_long(static_cast<long>(tv.tv_sec), 1704067200L, "epoch unchanged");
    expect_long(static_cast<long>(tv.tv_usec), 500000L, "usec 500000");

    // 1/256 s truncates to 3906 us.
    half[8] = 1;
    expect_true(cts_parse_current_time_to_timeval(half, sizeof(half), &tv), "tv 1/256");
    expect_long(static_cast<long>(tv.tv_usec), 3906L, "usec 3906");

    // 2026-08-31 19:49:00 UTC (this session's "now"), weekday ignored.
    const std::uint8_t k_session[] = {0xEA, 0x07, 8, 31, 19, 49, 0, 0, 0, 0};
    expect_true(cts_parse_current_time_to_timeval(k_session, sizeof(k_session), &tv), "tv session");
    expect_long(static_cast<long>(tv.tv_sec), 1788205740L, "epoch 2026-08-31");

    // Leap day.
    const std::uint8_t k_leap[] = {0xE8, 0x07, 2, 29, 12, 0, 0, 0, 0, 0};
    expect_true(cts_parse_current_time(k_leap, sizeof(k_leap), &ct), "2024-02-29");

    const std::uint8_t k_non_leap[] = {0xE7, 0x07, 2, 29, 12, 0, 0, 0, 0, 0};
    expect_true(!cts_parse_current_time(k_non_leap, sizeof(k_non_leap), &ct), "2023-02-29");

    expect_true(!cts_parse_current_time(k_new_year, 9, &ct), "short");
    expect_true(!cts_parse_current_time(k_new_year, 11, &ct), "long");
    expect_true(!cts_parse_current_time(nullptr, 10, &ct), "null data");
    expect_true(!cts_parse_current_time(k_new_year, 10, nullptr), "null out");

    const std::uint8_t k_month0[] = {0xE8, 0x07, 0, 1, 0, 0, 0, 0, 0, 0};
    expect_true(!cts_parse_current_time(k_month0, sizeof(k_month0), &ct), "month 0");

    const std::uint8_t k_hour24[] = {0xE8, 0x07, 1, 1, 24, 0, 0, 0, 0, 0};
    expect_true(!cts_parse_current_time(k_hour24, sizeof(k_hour24), &ct), "hour 24");

    const std::uint8_t k_year0[] = {0x00, 0x00, 1, 1, 0, 0, 0, 0, 0, 0};
    expect_true(!cts_parse_current_time(k_year0, sizeof(k_year0), &ct), "year 0");

    // Local Time Information 0x2A0F. Local = UTC + tz*15min + DST.
    CtsLocalTimeInfo lti = {};
    const std::uint8_t k_utc[] = {0, 0};
    expect_true(cts_parse_local_time_information(k_utc, sizeof(k_utc), &lti), "LTI UTC");
    expect_int(lti.timezone_15min, 0, "tz 0");
    expect_int(lti.dst_offset_seconds, 0, "dst 0");

    // US Eastern DST: tz=-20 (EST), DST=+1h. Local 2024-07-01 12:00 → 16:00 UTC.
    const std::uint8_t k_noon_edt[] = {0xE8, 0x07, 7, 1, 12, 0, 0, 1, 0, 0};
    const std::uint8_t k_est_dst[] = {static_cast<std::uint8_t>(-20), 0x04};
    expect_true(cts_parse_phone_time_to_utc_timeval(k_noon_edt, sizeof(k_noon_edt), k_est_dst,
                                                   sizeof(k_est_dst), &tv),
                "EDT noon");
    expect_long(static_cast<long>(tv.tv_sec), 1719849600L, "EDT → 16:00 UTC");

    // Same offset if the phone folds DST into timezone (-16) and DST=0.
    const std::uint8_t k_edt_folded[] = {static_cast<std::uint8_t>(-16), 0x00};
    expect_true(cts_parse_phone_time_to_utc_timeval(k_noon_edt, sizeof(k_noon_edt), k_edt_folded,
                                                   sizeof(k_edt_folded), &tv),
                "EDT folded tz");
    expect_long(static_cast<long>(tv.tv_sec), 1719849600L, "folded → 16:00 UTC");

    // EST winter: local 2024-01-01 00:00, tz=-20, DST=0 → 05:00 UTC.
    const std::uint8_t k_est[] = {static_cast<std::uint8_t>(-20), 0x00};
    expect_true(cts_parse_phone_time_to_utc_timeval(k_new_year, sizeof(k_new_year), k_est, sizeof(k_est),
                                                   &tv),
                "EST midnight");
    expect_long(static_cast<long>(tv.tv_sec), 1704085200L, "EST → 05:00 UTC");

    // IST +5:30 (tz=+22). Local 05:30 2024-01-01 → 00:00 UTC.
    const std::uint8_t k_ist_local[] = {0xE8, 0x07, 1, 1, 5, 30, 0, 1, 0, 0};
    const std::uint8_t k_ist[] = {22, 0};
    expect_true(cts_parse_phone_time_to_utc_timeval(k_ist_local, sizeof(k_ist_local), k_ist, sizeof(k_ist),
                                                   &tv),
                "IST 05:30");
    expect_long(static_cast<long>(tv.tv_sec), 1704067200L, "IST → 00:00 UTC");

    // Half-hour DST. tz=0, DST=+30 min. Local 00:30 → 00:00 UTC.
    const std::uint8_t k_half_local[] = {0xE8, 0x07, 1, 1, 0, 30, 0, 1, 0, 0};
    const std::uint8_t k_half_dst[] = {0, 0x02};
    expect_true(cts_parse_phone_time_to_utc_timeval(k_half_local, sizeof(k_half_local), k_half_dst,
                                                   sizeof(k_half_dst), &tv),
                "half DST");
    expect_long(static_cast<long>(tv.tv_sec), 1704067200L, "half DST → 00:00 UTC");

    expect_true(cts_parse_phone_time_to_utc_timeval(k_new_year, sizeof(k_new_year), k_utc, sizeof(k_utc),
                                                   &tv),
                "UTC LTI");
    expect_long(static_cast<long>(tv.tv_sec), 1704067200L, "UTC LTI unchanged");

    const std::uint8_t k_tz_unknown[] = {0x80, 0};
    expect_true(!cts_parse_local_time_information(k_tz_unknown, sizeof(k_tz_unknown), &lti), "tz unknown");

    const std::uint8_t k_dst_unknown[] = {0, 0xFF};
    expect_true(!cts_parse_local_time_information(k_dst_unknown, sizeof(k_dst_unknown), &lti),
                "dst unknown");

    const std::uint8_t k_tz_hi[] = {57, 0};
    expect_true(!cts_parse_local_time_information(k_tz_hi, sizeof(k_tz_hi), &lti), "tz 57");

    expect_true(!cts_parse_local_time_information(k_utc, 1, &lti), "LTI short");
    expect_true(!cts_parse_phone_time_to_utc_timeval(k_new_year, sizeof(k_new_year), k_tz_unknown,
                                                    sizeof(k_tz_unknown), &tv),
                "phone missing tz");

    if (g_fails != 0) {
        fprintf(stderr, "%d FAIL(s)\n", g_fails);
        return 1;
    }
    printf("PASS: cts_time\n");
    return 0;
}
