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

// ---------------------------------------------------------------------------
// Displayed battery percentage.
//
// The protection gates above and the number on screen want opposite things
// from the same measurement, and used to share one filter. A gate should react
// -- it has hysteresis and a hold to keep it honest. A percentage a human is
// watching should sit still.
//
// It has to sit still against unhelpful arithmetic: the Cardputer's map spans
// 0-100% over 800 mV, so **8 mV is one percent**, and ADC noise of +/-20 mV is
// +/-2-3% before anything else happens. Charging is the worst case, because the
// pack voltage genuinely is rising while the charge current modulates the rail
// -- so the one screen where the number is watched continuously is the one
// where it moves most.
//
// Two mechanisms, because either alone is not enough. A slow EMA cuts the noise
// roughly fourfold; a deadband then stops the last of the flicker at a boundary,
// where a true value of 50.4% would otherwise alternate 50/51 forever.
// ---------------------------------------------------------------------------

// Slow EMA over the display voltage. `state_mv` is the running value and is
// seeded on the first call (pass a negative value to seed). Returns the new
// running value; the caller keeps it.
int power_display_ema(int state_mv, int sample_mv);

// Deadband on the shown percentage: `shown` is kept until `candidate` differs
// by more than one point, so the display steps rather than jitters. Pass a
// negative `shown` on the first call to adopt the candidate immediately.
//
// The cost is honest and worth stating: the number can sit one point stale, and
// on a slow discharge it moves in twos. That is a better reading experience
// than a digit that will not settle, and it is never used for a decision --
// the halt gates read the fast filter, not this.
int power_display_percent(int shown, int candidate);
