#include <cstdio>
#include <cstring>
#include <cmath>
#include "tx_state_machine.h"
#include "decode_helper.h"
#include "../../components/ft8_lib/vendor/ft8/constants.h"

// Directory where failure WAVs are written (relative to CWD when running tests)
#ifndef FAIL_WAV_DIR
#define FAIL_WAV_DIR "."
#endif

// ---------------------------------------------------------------------------
// Test case descriptor
// ---------------------------------------------------------------------------
struct L2Case {
    const char*     label;
    TxConfig        cfg;
    int64_t         loop_delay_ms;
    ftx_protocol_t  decode_proto;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static int sym_ms_for(ftx_protocol_t proto) {
    return (proto == FTX_PROTOCOL_FT8)
        ? (int)lrintf(FT8_SYMBOL_PERIOD * 1000.0f)
        : (int)lrintf(FT4_SYMBOL_PERIOD * 1000.0f);
}

// Run one test case: timing check + encode→synthesise→decode round-trip.
// On decode failure, writes a WAV to FAIL_WAV_DIR for offline inspection.
// Returns true on full pass, prints a single result line per case.
static bool run_case(const L2Case& c, int case_idx) {
    decode_clear_hashes();
    auto events = run_tx(c.cfg, c.loop_delay_ms);

    // ── Timing assertion ──────────────────────────────────────────────────
    int sym_ms = sym_ms_for(c.cfg.protocol);
    std::string timing_err = verify_event_timing(events, c.cfg, sym_ms, c.loop_delay_ms);
    if (!timing_err.empty()) {
        printf("FAIL (timing: %s)\n", timing_err.c_str());
        return false;
    }

    // ── Decode round-trip ─────────────────────────────────────────────────
    auto pcm = events_to_pcm(events, 6000, 0.5f);
    DecodeResult res = decode_pcm(pcm.data(), (int)pcm.size(), 6000, c.decode_proto);
    if (res.found) {
        printf("PASS (SNR=%.1f)\n", res.snr);
        return true;
    }

    // ── Failure: dump WAV for offline inspection ──────────────────────────
    char wav_path[512];
    snprintf(wav_path, sizeof(wav_path), "%s/fail_l2_%02d.wav", FAIL_WAV_DIR, case_idx);
    int wav_rc = write_wav(wav_path, pcm.data(), (int)pcm.size(), 6000);
    if (wav_rc == 0)
        printf("FAIL (no decode) — WAV written to %s\n", wav_path);
    else
        printf("FAIL (no decode) — WAV write failed\n");
    return false;
}

// ---------------------------------------------------------------------------
// Test matrix
// ---------------------------------------------------------------------------
static const L2Case CASES[] = {

    // ── FT8 baseline ────────────────────────────────────────────────────────
    {
        "FT8 W1ABC K9XYZ FN42  skip=0 delay=2ms slot=0",
        { "FT8", FTX_PROTOCOL_FT8, 1500.0f, 0, 0, "W1ABC K9XYZ FN42" },
        2, FTX_PROTOCOL_FT8
    },
    {
        "FT8 W1ABC K9XYZ -12   skip=0 delay=5ms slot=0",
        { "FT8", FTX_PROTOCOL_FT8, 1500.0f, 0, 0, "W1ABC K9XYZ -12" },
        5, FTX_PROTOCOL_FT8
    },
    {
        "FT8 W1ABC K9XYZ RR73  skip=0 delay=1ms slot=15000ms",
        { "FT8", FTX_PROTOCOL_FT8, 1500.0f, 15000, 0, "W1ABC K9XYZ RR73" },
        1, FTX_PROTOCOL_FT8
    },
    {
        "FT8 W1ABC K9XYZ 73    skip=0 delay=2ms slot=99999000ms",
        { "FT8", FTX_PROTOCOL_FT8, 1500.0f, 99999000LL, 0, "W1ABC K9XYZ 73" },
        2, FTX_PROTOCOL_FT8
    },

    // ── FT8 CQ messages ─────────────────────────────────────────────────────
    // Tests the CQ message type (different payload layout to contact messages)
    {
        "FT8 CQ W1XYZ FN42     skip=0 delay=2ms",
        { "FT8", FTX_PROTOCOL_FT8, 1500.0f, 0, 0, "CQ W1XYZ FN42" },
        2, FTX_PROTOCOL_FT8
    },

    // ── FT8 edge frequencies ────────────────────────────────────────────────
    // Near the decoder's f_min (200 Hz) and f_max (2900 Hz) boundaries
    {
        "FT8 W1ABC K9XYZ RR73  skip=0 delay=2ms  300 Hz",
        { "FT8", FTX_PROTOCOL_FT8,  300.0f, 0, 0, "W1ABC K9XYZ RR73" },
        2, FTX_PROTOCOL_FT8
    },
    {
        "FT8 W1ABC K9XYZ RR73  skip=0 delay=2ms 2700 Hz",
        { "FT8", FTX_PROTOCOL_FT8, 2700.0f, 0, 0, "W1ABC K9XYZ RR73" },
        2, FTX_PROTOCOL_FT8
    },

    // ── FT8 skip_tones ──────────────────────────────────────────────────────
    // FT8 Costas arrays are at positions 0–6, 36–42, 72–78.
    // skip=7 loses the entire first Costas; two remain — should still decode.
    // Larger skips (e.g. 10+) are also validated by verify_event_timing() above
    // but are unreliable for decode once the initial wrong-freq prefix drowns the
    // second Costas window; only test what actually decodes reliably.
    {
        "FT8 W1ABC K9XYZ -12   skip=7  delay=2ms",
        { "FT8", FTX_PROTOCOL_FT8, 1500.0f, 0, 7, "W1ABC K9XYZ -12" },
        2, FTX_PROTOCOL_FT8
    },

    // ── FT4 baseline ────────────────────────────────────────────────────────
    {
        "FT4 W1ABC K9XYZ -12   skip=0 delay=2ms slot=0",
        { "FT4", FTX_PROTOCOL_FT4, 1500.0f, 0, 0, "W1ABC K9XYZ -12" },
        2, FTX_PROTOCOL_FT4
    },
    {
        "FT4 W1ABC K9XYZ RR73  skip=0 delay=5ms slot=7500ms",
        { "FT4", FTX_PROTOCOL_FT4, 1500.0f, 7500, 0, "W1ABC K9XYZ RR73" },
        5, FTX_PROTOCOL_FT4
    },

    // ── FT4 CQ messages ─────────────────────────────────────────────────────
    {
        "FT4 CQ W1XYZ FN42     skip=0 delay=2ms",
        { "FT4", FTX_PROTOCOL_FT4, 1500.0f, 0, 0, "CQ W1XYZ FN42" },
        2, FTX_PROTOCOL_FT4
    },

    // ── FT4 edge frequencies ────────────────────────────────────────────────
    {
        "FT4 W1ABC K9XYZ RR73  skip=0 delay=2ms  300 Hz",
        { "FT4", FTX_PROTOCOL_FT4,  300.0f, 0, 0, "W1ABC K9XYZ RR73" },
        2, FTX_PROTOCOL_FT4
    },
    {
        "FT4 W1ABC K9XYZ RR73  skip=0 delay=2ms 2700 Hz",
        { "FT4", FTX_PROTOCOL_FT4, 2700.0f, 0, 0, "W1ABC K9XYZ RR73" },
        2, FTX_PROTOCOL_FT4
    },

    // ── FT4 skip_tones ──────────────────────────────────────────────────────
    // FT4 Costas arrays are at positions 0–3, 49–52, 100–103.
    // skip=1 loses one Costas tone; three mostly intact — reliable decode.
    // skip=4 (entire first Costas) is boundary-tested by verify_event_timing()
    // but decode is unreliable once the initial wrong-freq prefix covers the
    // missing block; not included here.
    {
        "FT4 W1ABC K9XYZ -12   skip=1  delay=2ms",
        { "FT4", FTX_PROTOCOL_FT4, 1500.0f, 0, 1, "W1ABC K9XYZ -12" },
        2, FTX_PROTOCOL_FT4
    },
};

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main() {
    printf("L2 State Machine Test\n");
    printf("======================================\n\n");

    const int N = (int)(sizeof(CASES) / sizeof(CASES[0]));
    int passed = 0, failed = 0;

    for (int i = 0; i < N; i++) {
        printf("[%2d/%d] %s: ", i + 1, N, CASES[i].label);
        fflush(stdout);
        if (run_case(CASES[i], i + 1)) passed++;
        else                    failed++;
    }

    printf("\n======================================\n");
    printf("Results: %d/%d passed\n", passed, N);
    return (failed > 0) ? 1 : 0;
}
