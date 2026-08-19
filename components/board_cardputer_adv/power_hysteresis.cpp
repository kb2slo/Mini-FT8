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
