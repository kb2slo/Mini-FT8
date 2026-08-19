#pragma once

#include <cstdint>

// Dual-threshold hold: stay in `current` until ema stays at or below enter_mv
// for hold_us, or until ema reaches exit_mv. No ESP-IDF.

bool power_hold_hysteresis(bool current,
                           int ema_mv,
                           int enter_mv,
                           int exit_mv,
                           std::int64_t now_us,
                           std::int64_t hold_us,
                           std::int64_t* low_since_us);
