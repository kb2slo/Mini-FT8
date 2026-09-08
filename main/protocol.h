#pragma once

#include <stdint.h>
#include "ft8/constants.h"

// ---------------------------------------------------------------------------
// Runtime protocol configuration for Mini-FT8.
//
// g_protocol points to the active protocol's ProtocolConfig.  It is set once
// at boot by load_station_data() based on the protocol_mode field in
// Station.txt, and never changed during a session.  To switch protocols the
// user selects a new mode via Settings → Mode and reboots to apply.
//
// Boot-time set-once is simpler than hot-switching: no g_protocol_change_seq
// restart loop is needed in the audio pipeline.
// ---------------------------------------------------------------------------

struct ProtocolConfig {
    ftx_protocol_t protocol_id;  ///< FTX_PROTOCOL_FT8 or FTX_PROTOCOL_FT4
    const char*    name;         ///< "FT8" or "FT4"
    int            slot_time_ms; ///< Slot period in ms (15000 FT8, 7500 FT4)
    float          symbol_period;///< Symbol duration in seconds
    int            total_symbols;///< Total channel symbols (FT8_NN / FT4_NN)
    float          tone_spacing; ///< Tone spacing in Hz (6.25 FT8, 20.8333 FT4)
    uint32_t       samples_per_symbol; ///< Exact samples at 48 kHz UAC OUT
};

inline constexpr ProtocolConfig kProtocolFT8 = {
    .protocol_id   = FTX_PROTOCOL_FT8,
    .name          = "FT8",
    .slot_time_ms  = 15000,
    .symbol_period = FT8_SYMBOL_PERIOD,  // 0.160f
    .total_symbols = FT8_NN,
    .tone_spacing  = 6.25f,
    .samples_per_symbol = 7680,
};

inline constexpr ProtocolConfig kProtocolFT4 = {
    .protocol_id   = FTX_PROTOCOL_FT4,
    .name          = "FT4",
    .slot_time_ms  = 7500,
    .symbol_period = FT4_SYMBOL_PERIOD,  // 0.048f
    .total_symbols = FT4_NN,
    .tone_spacing  = 20.8333f,
    .samples_per_symbol = 2304,
};

// g_protocol — pointer to the active protocol for this boot session.
// Defined in main.cpp (defaults to &kProtocolFT8); set by load_station_data()
// if protocol_mode=FT4 is present in Station.txt.
extern const ProtocolConfig* g_protocol;
