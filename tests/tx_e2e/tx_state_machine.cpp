#include "tx_state_machine.h"
#include "../../components/ft8_lib/vendor/ft8/message.h"
#include "../../components/ft8_lib/vendor/ft8/encode.h"
#include "radio_ta_format.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern "C" {
    static bool stub_hash_lookup(ftx_callsign_hash_type_t, uint32_t, char*) { return false; }
    static void stub_hash_save(const char*, uint32_t) {}
}

std::vector<CatEvent> run_tx(const TxConfig& cfg, int64_t loop_delay_ms) {
    std::vector<CatEvent> events;
    int64_t now = cfg.slot_start_ms;

    // === tx_start: encode message ===
    ftx_message_t msg;
    ftx_callsign_hash_interface_t hash_if = {stub_hash_lookup, stub_hash_save};
    ftx_message_encode(&msg, &hash_if, cfg.text);

    // Encode payload to tones
    uint8_t tones[FT8_NN > FT4_NN ? FT8_NN : FT4_NN];
    int nn, sym_ms;
    float spacing;

    if (cfg.protocol == FTX_PROTOCOL_FT8) {
        ft8_encode(msg.payload, tones);
        nn     = FT8_NN;
        sym_ms = (int)lrintf(FT8_SYMBOL_PERIOD * 1000.0f);
        spacing = 6.25f;
    } else {
        ft4_encode(msg.payload, tones);
        nn     = FT4_NN;
        sym_ms = (int)lrintf(FT4_SYMBOL_PERIOD * 1000.0f);
        spacing = 20.8333f;
    }

    // Clamp skip_tones to valid range
    int tone_idx = cfg.skip_tones;
    if (tone_idx >= nn) tone_idx = nn;

    int64_t next_tone_time = cfg.slot_start_ms + (int64_t)tone_idx * sym_ms;

    // TX_BEGIN: radio enters TX mode at slot start
    events.push_back({now, CatEvent::TX_BEGIN, 0.0f});

    // Initial TA: mirrors firmware tx_start() — send the first tone frequency
    // immediately so the radio has a valid audio frequency the moment it is keyed,
    // rather than waiting for the first tx_tick() call.  tx_tick (the loop below)
    // will dedup this on its first fire.
    int last_ta_int = -1, last_ta_frac = -1;
    auto record_ta = [&](float hz) {
        int ta_int = 0;
        int ta_frac = 0;
        radio_ta_parts(hz, &ta_int, &ta_frac);
        if (ta_int != last_ta_int || ta_frac != last_ta_frac) {
            last_ta_int  = ta_int;
            last_ta_frac = ta_frac;
            events.push_back({now, CatEvent::TA, hz});
        }
    };

    if (tone_idx < nn) {
        float hz = cfg.base_hz + spacing * tones[tone_idx];
        // Push directly (not via record_ta) so last_ta remains -1/-1;
        // record_ta will then dedup naturally on the first tick.
        events.push_back({now, CatEvent::TA, hz});
    }

    // === tx_tick loop ===
    while (tone_idx < nn) {
        if (now < next_tone_time) {
            now += loop_delay_ms;
            continue;
        }
        float hz = cfg.base_hz + spacing * tones[tone_idx];
        record_ta(hz);
        tone_idx++;
        next_tone_time = cfg.slot_start_ms + (int64_t)tone_idx * sym_ms;
    }

    // TX_END
    events.push_back({now, CatEvent::TX_END, 0.0f});
    return events;
}

// ---------------------------------------------------------------------------
// Timing verifier
// ---------------------------------------------------------------------------
// Checks that every TA event emitted by the tick loop (i.e. every TA after
// the initial tx_start TA at slot_start_ms) arrives at a slot-anchored time:
//   (event.time_ms - slot_start_ms) % sym_ms  <=  loop_delay_ms
//
// This directly tests the slot-anchor formula used by tx_tick:
//   next_tone_time = slot_start_ms + tone_idx * sym_ms
//
// Returns an empty string on success, or a human-readable error on the first
// violation found.
std::string verify_event_timing(const std::vector<CatEvent>& events,
                                const TxConfig& cfg,
                                int sym_ms, int64_t loop_delay_ms)
{
    for (size_t i = 0; i < events.size(); i++) {
        if (events[i].kind != CatEvent::TA) continue;

        int64_t t = events[i].time_ms;
        // Skip the initial tx_start TA which is intentionally emitted at
        // slot_start_ms regardless of skip_tones.
        if (t <= cfg.slot_start_ms) continue;

        int64_t offset    = t - cfg.slot_start_ms;
        int64_t remainder = offset % sym_ms;
        if (remainder > loop_delay_ms) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "TA event %zu at t=%lld: offset_from_slot=%lld "
                "not slot-anchored (remainder %lld > loop_delay %lld, sym_ms=%d)",
                i, (long long)t, (long long)offset,
                (long long)remainder, (long long)loop_delay_ms, sym_ms);
            return std::string(buf);
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// CAT event stream → PCM (phase-continuous FSK)
// ---------------------------------------------------------------------------
std::vector<float> events_to_pcm(const std::vector<CatEvent>& events,
                                 int sample_rate, float amplitude)
{
    if (events.empty()) return {};

    int64_t t_start      = events.front().time_ms;
    int64_t t_end        = events.back().time_ms;
    int64_t total_ms     = t_end - t_start + 500;   // 500 ms tail silence
    int     total_samples = (int)((double)total_ms * sample_rate / 1000.0);

    std::vector<float> pcm(total_samples, 0.0f);

    double phase      = 0.0;
    float  current_hz = 0.0f;
    bool   tx_active  = false;

    size_t evt_idx = 0;
    for (int i = 0; i < total_samples; i++) {
        int64_t t_ms = t_start + (int64_t)((double)i * 1000.0 / sample_rate);

        while (evt_idx < events.size() && events[evt_idx].time_ms <= t_ms) {
            switch (events[evt_idx].kind) {
                case CatEvent::TX_BEGIN: tx_active  = true;                      break;
                case CatEvent::TA:       current_hz = events[evt_idx].tone_hz;   break;
                case CatEvent::TX_END:   tx_active  = false;                     break;
            }
            evt_idx++;
        }

        if (tx_active && current_hz > 0.0f) {
            pcm[i] = amplitude * sinf((float)phase);
            phase  += 2.0 * M_PI * current_hz / sample_rate;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
        }
    }

    return pcm;
}
