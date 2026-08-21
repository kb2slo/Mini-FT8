#pragma once

#include <cstdint>

// Decode-list dim (B10). Modest ~5/8 brightness after TX ends, and when an
// empty decode keeps the old list. Stay full brightness during TX so taps
// stay readable. A new non-empty list restores full brightness.
// No ESP-IDF. Host tests share it.

inline constexpr int kRxListDimNum = 5;
inline constexpr int kRxListDimDen = 8;

inline bool rx_list_should_dim(bool stale_until_new)
{
    return stale_until_new;
}

inline uint16_t rx_list_dim_rgb565(uint16_t color)
{
    unsigned r = (color >> 11) & 0x1fu;
    unsigned g = (color >> 5) & 0x3fu;
    unsigned b = color & 0x1fu;
    r = (r * (unsigned)kRxListDimNum) / (unsigned)kRxListDimDen;
    g = (g * (unsigned)kRxListDimNum) / (unsigned)kRxListDimDen;
    b = (b * (unsigned)kRxListDimNum) / (unsigned)kRxListDimDen;
    return (uint16_t)((r << 11) | (g << 5) | b);
}
