#pragma once

#include <cstdint>
#include <cstdio>

// TX HUD as a 1-row banner under the decode list (B10). Size-2 glyphs are 12px
// so 20 chars fill a 240px row. Message and meters swap unless abort or SWR
// warn freezes the status face. No ESP-IDF. Host tests share it.

inline constexpr int kTxHudBannerRows = 1;
inline constexpr int kTxHudBannerCols = 20;
inline constexpr int kTxHudBannerSwapMs = 1500;
inline constexpr float kTxHudBannerSwrWarn = 2.f;

struct TxHudBannerInput {
    const char* tx_text = "";
    int voltage_mv = -1;
    int percent = -1;
    bool writes_blocked = false;
    float power_w = -1.f;
    float swr = -1.f;
    bool tx_aborted = false;
};

inline int tx_hud_banner_list_rows(bool banner, bool sticky_abort, int total_rows)
{
    if (banner) {
        return total_rows - kTxHudBannerRows;
    }
    if (sticky_abort) {
        return total_rows - 1;
    }
    return total_rows;
}

inline bool tx_hud_banner_line_is_list(int line_0based, int list_rows)
{
    return line_0based >= 0 && line_0based < list_rows;
}

inline void tx_hud_banner_copy_trunc(char* dst, int cap, const char* src)
{
    if (!dst || cap <= 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    int n = 0;
    while (src[n] && n < cap - 1) {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}

inline void tx_hud_banner_format(const TxHudBannerInput& in, char* msg, char* status, int cap)
{
    tx_hud_banner_copy_trunc(msg, cap, in.tx_text);
    if (in.tx_aborted) {
        tx_hud_banner_copy_trunc(status, cap, "TX ABORT (low batt)");
        return;
    }

    char batt[16];
    batt[0] = '\0';
    if (in.percent >= 0) {
        snprintf(batt, sizeof(batt), "%d%%", in.percent);
    } else if (in.voltage_mv >= 0) {
        snprintf(batt, sizeof(batt), "%dmV", in.voltage_mv);
    }

    char body[32];
    body[0] = '\0';
    if (in.writes_blocked) {
        if (batt[0]) {
            snprintf(body, sizeof(body), "%s WR BLOCK", batt);
        } else {
            snprintf(body, sizeof(body), "WR BLOCK");
        }
    } else if (in.power_w >= 0.f && in.swr >= 0.f) {
        if (batt[0]) {
            snprintf(body, sizeof(body), "%.1fW %.2f %s",
                     (double)in.power_w, (double)in.swr, batt);
        } else {
            snprintf(body, sizeof(body), "%.1fW SWR %.2f",
                     (double)in.power_w, (double)in.swr);
        }
    } else if (in.power_w >= 0.f) {
        if (batt[0]) {
            snprintf(body, sizeof(body), "%.1fW %s", (double)in.power_w, batt);
        } else {
            snprintf(body, sizeof(body), "%.1fW", (double)in.power_w);
        }
    } else if (in.swr >= 0.f) {
        if (batt[0]) {
            snprintf(body, sizeof(body), "SWR %.2f %s", (double)in.swr, batt);
        } else {
            snprintf(body, sizeof(body), "SWR %.2f", (double)in.swr);
        }
    } else if (batt[0]) {
        snprintf(body, sizeof(body), "%s", batt);
    } else {
        snprintf(body, sizeof(body), "--");
    }
    tx_hud_banner_copy_trunc(status, cap, body);
}

inline bool tx_hud_banner_freeze_status(const TxHudBannerInput& in)
{
    if (in.tx_aborted || in.writes_blocked) {
        return true;
    }
    return in.swr >= kTxHudBannerSwrWarn;
}

inline bool tx_hud_banner_show_status(const TxHudBannerInput& in, int64_t now_ms)
{
    if (tx_hud_banner_freeze_status(in)) {
        return true;
    }
    if (now_ms < 0) {
        now_ms = 0;
    }
    return ((now_ms / kTxHudBannerSwapMs) % 2) == 1;
}
