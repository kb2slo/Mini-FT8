#include <cstdio>
#include <cstdint>

#include "rx_list_stale.h"

static int g_fails = 0;

static void expect_true(bool cond, const char* msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        ++g_fails;
    }
}

static void expect_u16(uint16_t got, uint16_t want, const char* msg)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got 0x%04x want 0x%04x)\n", msg, got, want);
        ++g_fails;
    }
}

int main()
{
    expect_true(!rx_list_should_dim(false), "fresh bright");
    expect_true(rx_list_should_dim(true), "stale after TX or empty keep");

    expect_u16(rx_list_dim_rgb565(0x0000), 0x0000, "black stays");
    // White 0xFFFF: R=31 G=63 B=31 → *5/8 → 19,39,19
    expect_u16(rx_list_dim_rgb565(0xFFFF), (uint16_t)((19u << 11) | (39u << 5) | 19u),
               "white modest gray");

    if (g_fails != 0) {
        fprintf(stderr, "%d FAIL(s)\n", g_fails);
        return 1;
    }
    printf("PASS: rx list stale\n");
    return 0;
}
