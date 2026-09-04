#include <cstdio>
#include <cstring>

#include "radio_profile.h"

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
    // I21 (2026-09-04): KH1_USBC/KH1_MIC dropped. Two-radio table now.
    expect_int(kRadioProfileCount, 2, "two product radios");
    expect_int((int)radio_profile_canonical(RadioType::NONE), (int)RadioType::QMX,
               "unknown is qmx");
    expect_int((int)radio_profile_canonical(RadioType::TRUSDX), (int)RadioType::QMX,
               "trusdx is qmx");
    expect_int((int)radio_profile_from_saved_int(5), (int)RadioType::QDX, "saved qdx");
    expect_int((int)radio_profile_from_saved_int(99), (int)RadioType::QMX, "bad saved");
    expect_int((int)radio_profile_from_saved_int(3), (int)RadioType::QMX,
               "retired kh1_usbc value falls back to qmx");
    expect_int((int)radio_profile_from_saved_int(4), (int)RadioType::QMX,
               "retired kh1_mic value falls back to qmx");

    expect_int((int)radio_profile_next(RadioType::QMX), (int)RadioType::QDX, "qmx next");
    expect_int((int)radio_profile_next(RadioType::QDX), (int)RadioType::QMX, "qdx wraps");

    expect_str(radio_profile_name(RadioType::QDX), "QDX", "qdx name");
    expect_true(radio_profile_audio_is_uac(RadioType::QMX), "qmx uac");
    expect_true(radio_profile_audio_is_uac(RadioType::QDX), "qdx uac");
    expect_true(radio_profile_get(RadioType::QDX).radio_backend == RADIO_CONTROL_QDX, "qdx cat");
    expect_true(radio_profile_get(RadioType::QMX).radio_backend == RADIO_CONTROL_QMX, "qmx cat");

    if (g_fails != 0) {
        fprintf(stderr, "%d FAIL(s)\n", g_fails);
        return 1;
    }
    printf("PASS: radio profile\n");
    return 0;
}
