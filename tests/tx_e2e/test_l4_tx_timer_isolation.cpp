// test_l4_tx_timer_isolation.cpp
// ──────────────────────────────────────────────────────────────────────────────
// L4 TX Timer-Isolation Test
//
// Validates the architectural property that moving tx_tick() from the main UI
// loop into a hardware timer (esp_timer) makes FT4 TX immune to display-update
// blocking.
//
// Two architectures are modelled deterministically:
//
//   COUPLED   — tx_tick() called from the main loop.
//               Effective poll period = timer_ms + ui_block_ms.
//               ui_block_ms represents time spent in ui_draw_waterfall() etc.
//               At ~10 µs per drawPixel call × 4,320 pixels (18 rows × 240 px)
//               the waterfall alone adds ~43 ms per loop iteration.
//
//   ISOLATED  — tx_tick() called from an esp_timer periodic callback.
//               Poll period = timer_ms exactly; ui_block_ms is irrelevant
//               because the timer fires independently of the UI task.
//
// The architectural invariant being tested:
//   ISOLATED + timer_ms ≤ 2 ms → max_late ≤ timer_ms regardless of ui_block_ms
//   COUPLED  + ui_block_ms ≥ 30 ms → ISI → decode failure
//
// Test cases (all use FT4, timer_ms = 2 ms):
//   COUPLED,  block =  0 ms  → PASS  (no blocking, 2 ms poll → zero jitter)
//   COUPLED,  block = 30 ms  → FAIL  (32 ms effective poll → ISI)
//   ISOLATED, block = 30 ms  → PASS  (timer immune to 30 ms UI blocking)
//   ISOLATED, block = 100 ms → PASS  (timer immune to 100 ms UI blocking)
//
// The 30 ms and 100 ms blocking figures bracket the real hardware condition:
// the M5Stack waterfall takes ~43 ms, and worst-case (full redraw every loop)
// the effective main-loop poll period reaches ~45 ms even with vTaskDelay(2).
// ──────────────────────────────────────────────────────────────────────────────

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include "decode_helper.h"
#include "timing_sim.h"
#include "../../components/ft8_lib/vendor/ft8/encode.h"
#include "../../components/ft8_lib/vendor/ft8/constants.h"

// ── Architecture model ───────────────────────────────────────────────────────

enum ArchModel { COUPLED, ISOLATED };

struct L4Case {
    const char*    text;
    ftx_protocol_t proto;
    float          base_hz;
    ArchModel      arch;
    int            timer_ms;      // nominal hw-timer or loop-delay period
    int            ui_block_ms;   // extra blocking per loop (0 for ideal)
    bool           should_decode;
    const char*    label;
};

// ── Test parameters ──────────────────────────────────────────────────────────

static const int SAMPLE_RATE = 6000;
static const float AMPLITUDE  = 0.5f;
static const int TIMER_MS     = 2;   // target poll period in both models

static const L4Case CASES[] = {
    // Baseline: COUPLED with no UI blocking.  Effective period = 2 ms.
    // 48 ms % 2 ms = 0 → zero jitter → must decode.
    { "CQ W1XYZ FN42", FTX_PROTOCOL_FT4, 1500.0f,
      COUPLED,  TIMER_MS, 0,
      true,
      "COUPLED   block=  0 ms: effective=2ms, 48%2=0 → zero jitter       [PASS]" },

    // COUPLED with 43 ms display blocking — the actual M5Stack condition.
    // ui_draw_waterfall() draws 18×240=4,320 pixels via drawPixel() at ~10 µs
    // each → ~43 ms per redraw.  Effective period = 2+43 = 45 ms.
    // 48 ms % 45 ms ≠ 0 → first tone fires at 90 ms (+88%) → hard ISI → FAIL.
    { "CQ W1XYZ FN42", FTX_PROTOCOL_FT4, 1500.0f,
      COUPLED,  TIMER_MS, 43,
      false,
      "COUPLED   block= 43 ms: effective=45ms (waterfall reality) → ISI   [FAIL]" },

    // ISOLATED with the same 43 ms display blocking.  Timer fires at 2 ms.
    // 48 ms % 2 ms = 0 → zero jitter regardless of waterfall cost.
    { "CQ W1XYZ FN42", FTX_PROTOCOL_FT4, 1500.0f,
      ISOLATED, TIMER_MS, 43,
      true,
      "ISOLATED  block= 43 ms: timer=2ms immune to waterfall blocking      [PASS]" },

    // ISOLATED with extreme 100 ms blocking.  Timer still fires at 2 ms.
    { "CQ W1XYZ FN42", FTX_PROTOCOL_FT4, 1500.0f,
      ISOLATED, TIMER_MS, 100,
      true,
      "ISOLATED  block=100 ms: timer=2ms immune to UI block               [PASS]" },
};
static const int N_CASES = (int)(sizeof(CASES) / sizeof(CASES[0]));

// ── Helpers ──────────────────────────────────────────────────────────────────

static const char* arch_name(ArchModel a)
{
    return (a == COUPLED) ? "COUPLED" : "ISOLATED";
}

static void print_case_stats(const JitterStats& js, ArchModel arch,
                              int timer_ms, int ui_block_ms)
{
    int effective = (arch == COUPLED) ? (timer_ms + ui_block_ms) : timer_ms;
    printf("  Model: %s  timer=%d ms  ui_block=%d ms  effective_poll=%d ms\n",
           arch_name(arch), timer_ms, ui_block_ms, effective);
    printf("  Jitter: max_late=%d ms  sym_dur=[%d..%d] ms  target=%d ms\n",
           js.max_late_ms, js.min_sym_dur_ms, js.max_sym_dur_ms, js.target_sym_ms);

    // Architectural invariant check
    if (arch == ISOLATED) {
        bool inv_ok = (js.max_late_ms <= timer_ms);
        printf("  Invariant (max_late ≤ timer_ms=%d): %s\n",
               timer_ms, inv_ok ? "SATISFIED" : "VIOLATED");
    }
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    printf("L4 TX Timer-Isolation Test\n");
    printf("======================================\n");
    printf("Validates that moving tx_tick() into an esp_timer callback makes\n");
    printf("FT4 TX immune to display-update blocking (the hardware root cause).\n");
    printf("COUPLED model: effective_poll = timer_ms + ui_block_ms\n");
    printf("ISOLATED model: effective_poll = timer_ms (UI blocking irrelevant)\n\n");

    int passed = 0, failed = 0, unexpected = 0;

    for (int ci = 0; ci < N_CASES; ci++) {
        const L4Case& c = CASES[ci];
        const bool  is_ft4   = (c.proto == FTX_PROTOCOL_FT4);
        const int   nn       = is_ft4 ? FT4_NN : FT8_NN;
        const int   sym_ms   = is_ft4 ? (int)lrintf(FT4_SYMBOL_PERIOD * 1000.0f)
                                       : (int)lrintf(FT8_SYMBOL_PERIOD * 1000.0f);
        const float spacing  = is_ft4 ? 20.8333f : 6.25f;

        printf("[%d/%d] %s\n", ci + 1, N_CASES, c.label);
        fflush(stdout);

        // ── Encode ────────────────────────────────────────────────────────
        decode_clear_hashes();
        ftx_message_t msg;
        ftx_message_rc_t rc = ftx_message_encode(&msg, decode_get_hash_if(), c.text);
        if (rc != FTX_MESSAGE_RC_OK) {
            printf("  ABORT: encode failed (%d)\n\n", (int)rc);
            failed++;
            continue;
        }

        uint8_t tones[FT8_NN > FT4_NN ? FT8_NN : FT4_NN];
        if (is_ft4) ft4_encode(msg.payload, tones);
        else        ft8_encode(msg.payload, tones);

        // ── Simulate poll timing ───────────────────────────────────────────
        // ISOLATED: timer fires every timer_ms regardless of ui_block_ms
        // COUPLED:  loop period = timer_ms + ui_block_ms
        int effective_poll = (c.arch == ISOLATED)
                           ? c.timer_ms
                           : (c.timer_ms + c.ui_block_ms);

        std::vector<int> fire_ms = simulate_poll(effective_poll, nn, sym_ms);
        JitterStats js = compute_jitter(fire_ms, nn, sym_ms);
        print_case_stats(js, c.arch, c.timer_ms, c.ui_block_ms);

        // ── Synthesise audio from actual fire times ───────────────────────
        std::vector<float> pcm = fire_times_to_pcm(
            fire_ms, tones, nn,
            c.base_hz, spacing,
            SAMPLE_RATE, AMPLITUDE,
            sym_ms, /*pad_ms=*/500);

        // ── Decode ────────────────────────────────────────────────────────
        decode_clear_hashes();
        ftx_message_encode(&msg, decode_get_hash_if(), c.text);
        DecodeResult res = decode_pcm(pcm.data(), (int)pcm.size(),
                                      SAMPLE_RATE, c.proto);

        // ── Evaluate ──────────────────────────────────────────────────────
        bool decoded = res.found;
        const char* outcome;
        if (decoded == c.should_decode) {
            if (decoded) {
                outcome = "PASS";
            } else {
                outcome = "PASS (expected ISI failure confirmed)";
            }
            passed++;
        } else {
            outcome = decoded ? "UNEXPECTED PASS (ISI may be fixed elsewhere?)"
                              : "FAIL (unexpected decode failure)";
            unexpected++;
            failed++;
        }

        if (decoded) {
            printf("  Decoded: '%s'  SNR=%.1f dB\n", res.text, res.snr);
        } else {
            printf("  No decode (LDPC did not converge — ISI signature)\n");
            char wav[128];
            snprintf(wav, sizeof(wav), "fail_l4_%02d_%s_block%dms.wav",
                     ci + 1, arch_name(c.arch), c.ui_block_ms);
            if (write_wav(wav, pcm.data(), (int)pcm.size(), SAMPLE_RATE) == 0)
                printf("  WAV written: %s\n", wav);
        }
        printf("  Result: %s\n\n", outcome);
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    printf("======================================\n");
    printf("Results: %d/%d passed", passed, N_CASES);
    if (unexpected > 0)
        printf("  (%d unexpected outcomes)", unexpected);
    printf("\n\n");

    // Print the key architectural conclusion
    printf("Architectural conclusion:\n");
    printf("  COUPLED + waterfall blocking (~43 ms) → effective_poll = 45 ms\n");
    printf("    48 ms %% 45 ms ≠ 0 → first tone fires 90 ms late → hard ISI\n");
    printf("    LDPC cannot converge: this is the hardware failure mechanism.\n");
    printf("  ISOLATED + timer_ms = 2 ms → effective_poll = 2 ms always\n");
    printf("    48 ms %% 2 ms = 0 → zero jitter → decode succeeds regardless\n");
    printf("    of how long ui_draw_waterfall() blocks the main loop.\n");
    printf("  Fix: move tx_tick() into esp_timer_create() periodic callback.\n");
    return (failed > 0) ? 1 : 0;
}
