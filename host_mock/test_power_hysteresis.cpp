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

int main()
{
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
