#pragma once

#include <cmath>
#include <cstdio>

// QMX CAT audio-frequency command: TA%04d.%02d;  e.g. "TA1520.83;"
//
// floorf (not lrintf) for the integer field so frac stays in [0.0, 1.0) for
// in-range tones. lrintf(1520.83) is 1521 and formats as "TA1521.-17;".
// Clamp ta_frac to 0..99 so %02d cannot emit 100 or a minus. Clamp ta_int
// to 0..9999, the documented TA range.

static inline void radio_ta_parts(float tone_hz, int* ta_int, int* ta_frac)
{
    int whole = (int)floorf(tone_hz);
    if (whole < 0) {
        whole = 0;
    }
    if (whole > 9999) {
        whole = 9999;
    }
    const float frac = tone_hz - (float)whole;
    int hundredths = (int)lrintf(frac * 100.0f);
    if (hundredths < 0) {
        hundredths = 0;
    }
    if (hundredths > 99) {
        hundredths = 99;
    }
    *ta_int = whole;
    *ta_frac = hundredths;
}

static inline void radio_ta_format(float tone_hz, char* out, int out_len)
{
    int ta_int = 0;
    int ta_frac = 0;
    radio_ta_parts(tone_hz, &ta_int, &ta_frac);
    snprintf(out, out_len, "TA%04d.%02d;", ta_int, ta_frac);
}
