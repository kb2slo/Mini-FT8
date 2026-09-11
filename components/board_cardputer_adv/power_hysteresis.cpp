#include "power_hysteresis.h"

bool power_hold_hysteresis(bool current,
                           int ema_mv,
                           int enter_mv,
                           int exit_mv,
                           std::int64_t now_us,
                           std::int64_t hold_us,
                           std::int64_t* low_since_us)
{
    if (ema_mv <= enter_mv) {
        if (*low_since_us == 0) {
            *low_since_us = now_us;
        }
        if ((now_us - *low_since_us) >= hold_us) {
            return true;
        }
        return current;
    }
    *low_since_us = 0;
    if (ema_mv >= exit_mv) {
        return false;
    }
    return current;
}

// 15/16 against the gates' 3/4. Sixteen samples of memory instead of four cuts
// noise by about a factor of four -- enough that ADC wander lands inside one
// percent -- while still reaching a genuinely new level within a few seconds at
// the sampling rate this runs at.
static constexpr int kDisplayEmaNum = 15;
static constexpr int kDisplayEmaDen = 16;

int power_display_ema(int state_mv, int sample_mv)
{
    if (state_mv < 0) {
        return sample_mv;
    }
    return (state_mv * kDisplayEmaNum + sample_mv) / kDisplayEmaDen;
}

int power_display_percent(int shown, int candidate)
{
    if (shown < 0) {
        return candidate;
    }
    const int delta = candidate > shown ? candidate - shown : shown - candidate;
    // 0 and 100 are the two values worth being exact about: "full" and "empty"
    // are read as statements, not measurements, so let them through unfiltered.
    if (candidate == 0 || candidate == 100) {
        return candidate;
    }
    return (delta > 1) ? candidate : shown;
}
