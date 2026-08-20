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
    expect_int(kRadioProfileCount, 4, "four product radios");
    expect_int((int)radio_profile_canonical(RadioType::NONE), (int)RadioType::QMX,
               "unknown is qmx");
    expect_int((int)radio_profile_canonical(RadioType::TRUSDX), (int)RadioType::QMX,
               "trusdx is qmx");
    expect_int((int)radio_profile_from_saved_int(5), (int)RadioType::QDX, "saved qdx");
    expect_int((int)radio_profile_from_saved_int(99), (int)RadioType::QMX, "bad saved");

    expect_int((int)radio_profile_next(RadioType::QMX), (int)RadioType::QDX, "qmx next");
    expect_int((int)radio_profile_next(RadioType::QDX), (int)RadioType::KH1_USBC, "qdx next");
    expect_int((int)radio_profile_next(RadioType::KH1_USBC), (int)RadioType::KH1_MIC,
               "usbc next");
    expect_int((int)radio_profile_next(RadioType::KH1_MIC), (int)RadioType::QMX, "mic wrap");

    expect_str(radio_profile_name(RadioType::KH1_USBC), "KH1-USBC", "usbc name");
    expect_true(radio_profile_audio_is_uac(RadioType::QMX), "qmx uac");
    expect_true(radio_profile_audio_is_uac(RadioType::KH1_USBC), "kh1 usbc uac");
    expect_true(!radio_profile_audio_is_uac(RadioType::KH1_MIC), "kh1 mic not uac");
    expect_true(radio_profile_get(RadioType::KH1_USBC).radio_backend == RADIO_CONTROL_KH1_CAT,
                "usbc cat");
    expect_true(radio_profile_get(RadioType::QDX).radio_backend == RADIO_CONTROL_QDX, "qdx cat");

    expect_true(radio_profile_shares_porta_uart(RadioType::KH1_MIC), "kh1 porta");
    expect_true(!radio_profile_shares_porta_uart(RadioType::QMX), "qmx not porta");
    expect_true(radio_profile_needs_manual_connect(RadioType::KH1_USBC), "kh1 connect");
    expect_true(radio_profile_defer_cat_on_audio_start(RadioType::KH1_USBC), "usbc defer");
    expect_true(!radio_profile_defer_cat_on_audio_start(RadioType::KH1_MIC), "mic no defer");

    expect_true(radio_profile_porta_gps_should_run(RadioType::QMX, true, false),
                "qmx gps always");
    expect_true(radio_profile_porta_gps_should_run(RadioType::KH1_USBC, false, false),
                "kh1 gps until connect");
    expect_true(!radio_profile_porta_gps_should_run(RadioType::KH1_USBC, true, false),
                "kh1 connected takes porta");
    expect_true(radio_profile_porta_gps_should_run(RadioType::KH1_USBC, true, true),
                "kh1 plus gnss lora");

    if (g_fails != 0) {
        fprintf(stderr, "%d FAIL(s)\n", g_fails);
        return 1;
    }
    printf("PASS: radio profile\n");
    return 0;
}
