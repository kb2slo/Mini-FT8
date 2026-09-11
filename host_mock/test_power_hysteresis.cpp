#include <cstdint>
#include <cstdio>

#include "power_hysteresis.h"

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

// The charge screen was jumping because the gates' fast filter also drove the
// displayed percentage. These pin the two mechanisms that stop it: a slow EMA,
// and a deadband for the boundary case an EMA cannot fix on its own.
static void test_display_smoothing()
{
    // Seeds on the first sample rather than ramping up from zero.
    expect_true(power_display_ema(-1, 3900) == 3900, "display EMA seeds on first sample");

    // 8 mV is one percent on this map, so one noisy sample must not move the
    // filtered value by anything like its own size.
    int ema = 3900;
    ema = power_display_ema(ema, 3900 + 160);   // a 20-point excursion
    expect_true(ema - 3900 <= 10, "a single noisy sample moves the display EMA by about 1%");

    // It must still reach a genuinely new level, or the number would be a lie.
    ema = 3900;
    for (int i = 0; i < 60; ++i) {
        ema = power_display_ema(ema, 3700);
    }
    expect_true(ema <= 3710, "a sustained change is reached, not resisted forever");

    // Deadband: the boundary case. A true value between two percentages must
    // not alternate between them.
    expect_true(power_display_percent(-1, 50) == 50, "first reading is adopted as-is");
    expect_true(power_display_percent(50, 51) == 50, "one point of movement is held");
    expect_true(power_display_percent(50, 49) == 50, "held in both directions");
    expect_true(power_display_percent(50, 52) == 52, "two points moves");
    expect_true(power_display_percent(50, 48) == 48, "two points moves downward too");

    // Full and empty are read as statements, not measurements.
    expect_true(power_display_percent(99, 100) == 100, "100% is never withheld");
    expect_true(power_display_percent(1, 0) == 0, "0% is never withheld");
}

int main()
{
    test_display_smoothing();

    const int enter_mv = 3400;
    const int exit_mv = 3550;
    const std::int64_t hold_us = 8 * 1000 * 1000;

    {
        std::int64_t low_since = 0;
        bool halted = false;
        halted = power_hold_hysteresis(halted, 3600, enter_mv, exit_mv, 0, hold_us, &low_since);
        expect_true(!halted, "healthy pack stays running");
        expect_true(low_since == 0, "healthy pack clears timer");
    }

    {
        std::int64_t low_since = 0;
        bool halted = false;
        halted = power_hold_hysteresis(halted, 3300, enter_mv, exit_mv, 1000, hold_us, &low_since);
        expect_true(!halted, "brief sag does not halt");
        expect_true(low_since == 1000, "sag starts hold timer");
        halted = power_hold_hysteresis(halted, 3300, enter_mv, exit_mv, 1000 + hold_us - 1,
                                      hold_us, &low_since);
        expect_true(!halted, "still holding just before window");
        halted = power_hold_hysteresis(halted, 3300, enter_mv, exit_mv, 1000 + hold_us,
                                      hold_us, &low_since);
        expect_true(halted, "halt after hold window");
    }

    {
        std::int64_t low_since = 0;
        bool halted = true;
        halted = power_hold_hysteresis(halted, 3500, enter_mv, exit_mv, 0, hold_us, &low_since);
        expect_true(halted, "band between enter and exit keeps halt");
        halted = power_hold_hysteresis(halted, 3550, enter_mv, exit_mv, 1, hold_us, &low_since);
        expect_true(!halted, "exit threshold clears halt");
        expect_true(low_since == 0, "exit clears timer");
    }

    {
        std::int64_t low_since = 0;
        bool halted = false;
        halted = power_hold_hysteresis(halted, 3300, enter_mv, exit_mv, 0, hold_us, &low_since);
        halted = power_hold_hysteresis(halted, 3500, enter_mv, exit_mv, 1000, hold_us, &low_since);
        expect_true(!halted, "recover into the band before hold expires");
        expect_true(low_since == 0, "leaving enter region resets timer");
        halted = power_hold_hysteresis(halted, 3300, enter_mv, exit_mv, 2000, hold_us, &low_since);
        expect_true(!halted, "new sag restarts hold");
        expect_true(low_since == 2000, "timer restarts after bounce");
    }

    if (g_fails != 0) {
        fprintf(stderr, "%d failure(s)\n", g_fails);
        return 1;
    }
    printf("PASS: power hysteresis\n");
    return 0;
}
