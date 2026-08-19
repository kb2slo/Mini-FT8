#include <cstdio>
#include <cstring>
#include <string>

#include "station.h"

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

static void expect_float(float got, float want, const char* msg)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %f want %f)\n", msg, got, want);
        ++g_fails;
    }
}

int main()
{
    {
        StationSettings s;
        station_settings_init(&s);
        station_parse("", &s);
        expect_str(s.call, "", "empty call");
        expect_int(s.radio, 2, "default radio QMX");
        expect_int(s.gps_baud, 115200, "default gps baud");
    }

    {
        StationSettings s;
        station_settings_init(&s);
        station_parse(
            "call= kb2slo\n"
            "grid=fn30pr\n"
            "date=2026-08-19\n"
            "time=18:04:01\n"
            "band5=14074\n"
            "radio=QDX\n"
            "cq_type=4\n"
            "beacon=1\n"
            "unknown_key=nope\n",
            &s);
        expect_str(s.call, "KB2SLO", "call upper");
        expect_str(s.grid, "FN30pr", "grid maidenhead");
        expect_str(s.date, "2026-08-19", "date not overlapping line");
        expect_str(s.time, "18:04:01", "time hms");
        expect_true(s.band_freq_set[5], "band5 set");
        expect_float(s.band_freq[5], 14074.0f, "band5 freq");
        expect_int(s.radio, 5, "radio QDX");
        expect_int(s.cq_type, 4, "cq FD");
        expect_true(!s.protocol_ft4, "beacon must not imply ft4");
    }

    {
        StationSettings s;
        station_settings_init(&s);
        station_parse("protocol_mode=FT4\nft4_band5=14080\nband5=14074\n", &s);
        expect_true(s.protocol_ft4, "ft4 mode");
        expect_true(s.ft4_band_freq_set[5], "ft4 band5");
        expect_float(s.ft4_band_freq[5], 14080.0f, "ft4 freq");
        expect_true(s.band_freq_set[5], "ft8 band5 also parsed");
        expect_float(s.band_freq[5], 14074.0f, "ft8 freq kept");
        s.serialize_ft4_band_keys = true;
        const std::string text = station_serialize(s);
        expect_true(text.find("protocol_mode=FT4") != std::string::npos, "serialize ft4");
        expect_true(text.find("ft4_band5=14080") != std::string::npos, "serialize ft4 band");
        expect_true(text.find("band5=14074") == std::string::npos, "serialize omits inactive prefix");
        expect_true(text.find("beacon=") == std::string::npos, "no beacon line");
    }

    {
        StationSettings s;
        station_settings_init(&s);
        s.call = "KB2SLO";
        s.grid = "FN30";
        s.date = "2026-08-19";
        s.time = "01:02:03";
        s.band_freq[0] = 1840.0f;
        s.band_freq_set[0] = true;
        s.radio = 2;
        const std::string text = station_serialize(s);
        StationSettings round;
        station_settings_init(&round);
        station_parse(text, &round);
        expect_str(round.call, "KB2SLO", "round-trip call");
        expect_str(round.grid, "FN30", "round-trip grid");
        expect_str(round.date, "2026-08-19", "round-trip date");
        expect_str(round.time, "01:02:03", "round-trip time");
        expect_float(round.band_freq[0], 1840.0f, "round-trip band0");
        expect_int(round.radio, 2, "round-trip radio");
    }

    {
        StationSettings s;
        station_settings_init(&s);
        station_parse("gps_source=2\nactive_band=5\n", &s);
        expect_true(s.gnss_lora, "legacy gps_source=2");
        expect_str(s.active_bands, "5", "legacy active_band");
    }

    if (g_fails) {
        fprintf(stderr, "%d FAIL(s)\n", g_fails);
        return 1;
    }
    printf("PASS: station parse/serialize\n");
    return 0;
}
