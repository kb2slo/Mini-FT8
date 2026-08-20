// test_wav_diag.cpp
// ──────────────────────────────────────────────────────────────────────────────
// Comprehensive diagnostic tool for analysing a golden WAV that fails to decode.
//
// Useful when a real device recording doesn't decode in test_golden_rx.
// Reports: signal stats, candidate scores, LDPC/CRC status, frequency sweep,
// symbol-period probe, and optionally writes a 6 kHz resampled copy.
//
// Usage (run directly, not via ctest):
//   test_wav_diag <file.wav> [expected_message] [ft8|ft4]
//
// Example:
//   test_wav_diag ../golden/ft4_cq_w1xyz_fn42.wav "CQ W1XYZ FN42" ft4
// ──────────────────────────────────────────────────────────────────────────────

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>
#include "decode_helper.h"
#include "../../components/ft8_lib/vendor/ft8/decode.h"
#include "../../components/ft8_lib/vendor/ft8/constants.h"
#include "../../components/ft8_lib/common/monitor.h"

// MONITOR_NFFT_MAX is the hard static-buffer limit in monitor.c
#define MY_NFFT_MAX 960

// ─────────────────────────────────────────────────────────────────────────────
// Signal statistics
// ─────────────────────────────────────────────────────────────────────────────
struct SigStats {
    float peak;
    float rms;
    int   clipped;      // samples at or above ±1.0
    float duration_s;
    int   n_samples;
    int   fs;
};

static SigStats compute_stats(const float* s, int n, int fs)
{
    SigStats st = {};
    st.n_samples  = n;
    st.fs         = fs;
    st.duration_s = (float)n / fs;
    double sum_sq = 0.0;
    for (int i = 0; i < n; i++) {
        float a = fabsf(s[i]);
        if (a > st.peak) st.peak = a;
        if (a >= 0.9999f) st.clipped++;
        sum_sq += (double)(s[i] * s[i]);
    }
    st.rms = (float)sqrt(sum_sq / n);
    return st;
}

// ─────────────────────────────────────────────────────────────────────────────
// Downsampler: simple 2:1 decimation with a basic anti-alias FIR
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<float> decimate2(const std::vector<float>& in)
{
    // 7-tap half-band FIR coefficients (symmetric, sum≈1)
    static const float h[] = { 0.0625f, 0.0f, -0.0625f, 0.5f, -0.0625f, 0.0f, 0.0625f };
    // Actually let's use a simple 4-tap boxcar for clarity
    // Better: true anti-alias — average 4 samples at a time
    // We'll keep it simple: average pairs
    std::vector<float> out;
    out.reserve(in.size() / 2);
    for (size_t i = 0; i + 1 < in.size(); i += 2) {
        out.push_back((in[i] + in[i + 1]) * 0.5f);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Full decode with detailed diagnostics
// ─────────────────────────────────────────────────────────────────────────────
struct DiagResult {
    bool    decoded;
    char    text[256];
    int     num_candidates;
    // Per-candidate details (up to 20)
    struct Cand {
        int   score;
        int   time_offset;
        int   freq_offset;
        int   time_sub;
        int   freq_sub;
        float freq_hz;
        int   ldpc_errors;
        bool  crc_ok;
        bool  decoded;
    } cands[20];
};

static DiagResult decode_detailed(const float* samples, int n_samples, int fs,
                                   ftx_protocol_t proto,
                                   float f_min, float f_max,
                                   int time_osr, int freq_osr,
                                   int max_ldpc_iter)
{
    DiagResult r = {};

    // Guard: nfft must not exceed MONITOR_NFFT_MAX
    float sym_period = (proto == FTX_PROTOCOL_FT4) ? FT4_SYMBOL_PERIOD : FT8_SYMBOL_PERIOD;
    int   block_size = (int)(fs * sym_period);
    int   nfft       = block_size * freq_osr;
    if (nfft > MY_NFFT_MAX) {
        printf("    [SKIP] nfft=%d exceeds MONITOR_NFFT_MAX=%d (fs=%d freq_osr=%d)\n",
               nfft, MY_NFFT_MAX, fs, freq_osr);
        return r;
    }

    monitor_config_t cfg = {
        .f_min       = f_min,
        .f_max       = f_max,
        .sample_rate = fs,
        .time_osr    = time_osr,
        .freq_osr    = freq_osr,
        .protocol    = proto
    };

    monitor_t mon;
    monitor_init(&mon, &cfg);
    monitor_reset(&mon);

    for (int i = 0; i + mon.block_size <= n_samples; i += mon.block_size)
        monitor_process(&mon, samples + i);

    ftx_candidate_t heap[20];
    int nc = ftx_find_candidates(&mon.wf, 20, heap, 0);
    r.num_candidates = nc;

    // Sort by score descending for readability
    for (int a = 0; a < nc - 1; a++)
        for (int b = a + 1; b < nc; b++)
            if (heap[b].score > heap[a].score) std::swap(heap[a], heap[b]);

    for (int i = 0; i < nc && i < 20; i++) {
        ftx_message_t msg;
        ftx_decode_status_t status = {};
        bool ok = ftx_decode_candidate(&mon.wf, &heap[i], max_ldpc_iter, &msg, &status);

        DiagResult::Cand& c = r.cands[i];
        c.score       = heap[i].score;
        c.time_offset = heap[i].time_offset;
        c.freq_offset = heap[i].freq_offset;
        c.time_sub    = heap[i].time_sub;
        c.freq_sub    = heap[i].freq_sub;
        c.ldpc_errors = status.ldpc_errors;
        c.crc_ok      = (status.crc_extracted == status.crc_calculated);
        c.decoded     = ok;

        // Estimated frequency: bin_hz * freq_offset (bin 0 = f_min)
        float bin_hz = 1.0f / sym_period;   // always tone-spacing, regardless of fs
        c.freq_hz    = f_min + heap[i].freq_offset * bin_hz;

        if (ok && !r.decoded) {
            char text[256];
            ftx_message_offsets_t offsets = {};
            ftx_callsign_hash_interface_t* hash_if = decode_get_hash_if();
            ftx_message_decode(&msg, hash_if, text, &offsets);
            normalize_text(text, r.text, sizeof(r.text));
            r.decoded = true;
        }
    }

    monitor_free(&mon);
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol-period probe: for each possible symbol period (ms), measure how well
// energy transitions align with tone boundaries.  Reports top-5.
// ─────────────────────────────────────────────────────────────────────────────
static void probe_symbol_period(const float* samples, int n_samples, int fs,
                                 float center_hz, float bandwidth_hz)
{
    printf("\n── Symbol-period probe (energy around %.0f±%.0f Hz) ──\n",
           (double)center_hz, (double)bandwidth_hz);

    // Compute per-sample energy in the band via a simple DFT over short windows
    const int window_ms = 5;
    const int win_samps = (int)(fs * window_ms / 1000.0f);
    if (win_samps < 2) { printf("  window too small\n"); return; }

    // Candidate symbol periods to test (ms)
    const float test_periods_ms[] = { 44.0f, 45.0f, 46.0f, 47.0f, 47.5f, 48.0f,
                                       48.5f, 49.0f, 50.0f, 160.0f, 161.0f };
    const int n_periods = (int)(sizeof(test_periods_ms)/sizeof(test_periods_ms[0]));

    // Build energy envelope: average power per window
    int n_windows = n_samples / win_samps;
    std::vector<float> energy(n_windows, 0.0f);
    int f_lo = (int)(center_hz - bandwidth_hz / 2.0f);
    int f_hi = (int)(center_hz + bandwidth_hz / 2.0f);

    for (int w = 0; w < n_windows; w++) {
        const float* frame = samples + w * win_samps;
        // Compute DFT magnitude at target bins
        float e = 0.0f;
        for (int f = f_lo; f <= f_hi; f++) {
            float re = 0.0f, im = 0.0f;
            for (int k = 0; k < win_samps; k++) {
                float phase = 2.0f * 3.14159265f * f * k / fs;
                re += frame[k] * cosf(phase);
                im += frame[k] * sinf(phase);
            }
            e += re*re + im*im;
        }
        energy[w] = e;
    }

    // For each candidate period, compute autocorrelation of energy envelope
    printf("  %-12s  %s\n", "Period (ms)", "Autocorr score");
    printf("  %-12s  %s\n", "-----------", "--------------");

    struct PeriodScore { float period_ms; float score; };
    std::vector<PeriodScore> scores;

    for (int p = 0; p < n_periods; p++) {
        float period_ms = test_periods_ms[p];
        int lag_windows = (int)(period_ms / window_ms + 0.5f);
        if (lag_windows <= 0 || lag_windows >= n_windows) continue;

        float corr = 0.0f, norm = 0.0f;
        for (int w = 0; w < n_windows - lag_windows; w++) {
            corr += energy[w] * energy[w + lag_windows];
            norm += energy[w] * energy[w];
        }
        if (norm > 0.0f) corr /= norm;
        scores.push_back({period_ms, corr});
    }

    std::sort(scores.begin(), scores.end(),
              [](const PeriodScore& a, const PeriodScore& b){ return b.score < a.score; });

    for (auto& ps : scores)
        printf("  %-12.1f  %.6f\n", (double)ps.period_ms, (double)ps.score);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frequency sweep: try decoding with progressively narrower frequency windows
// to find where the signal actually is
// ─────────────────────────────────────────────────────────────────────────────
static void frequency_sweep(const float* samples, int n_samples, int fs,
                              ftx_protocol_t proto, int max_ldpc_iter)
{
    printf("\n── Frequency window sweep ──\n");
    printf("  %-22s  %-6s  %-6s  %s\n", "Window (Hz)", "Cands", "Top score", "Best candidate");
    printf("  %-22s  %-6s  %-6s  %s\n", "──────────────────────", "─────", "─────────", "──────────────");

    struct Window { float lo, hi; };
    const Window windows[] = {
        {  100.0f, 3000.0f },  // broad
        {  500.0f, 2500.0f },  // standard
        { 1000.0f, 2000.0f },  // narrow around 1500
        { 1200.0f, 1800.0f },  // very narrow
        { 1400.0f, 1700.0f },  // tight
    };
    const int n_windows = (int)(sizeof(windows)/sizeof(windows[0]));

    for (int w = 0; w < n_windows; w++) {
        decode_clear_hashes();
        DiagResult r = decode_detailed(samples, n_samples, fs, proto,
                                        windows[w].lo, windows[w].hi,
                                        2, 1, max_ldpc_iter);

        char label[32];
        snprintf(label, sizeof(label), "%.0f–%.0f Hz", (double)windows[w].lo, (double)windows[w].hi);

        int top_score = (r.num_candidates > 0) ? r.cands[0].score : 0;
        char best[64] = "—";
        if (r.num_candidates > 0)
            snprintf(best, sizeof(best), "t=%d f=%.0f ldpc=%d crc=%s",
                     r.cands[0].time_offset,
                     (double)r.cands[0].freq_hz,
                     r.cands[0].ldpc_errors,
                     r.cands[0].crc_ok ? "OK" : "FAIL");

        printf("  %-22s  %-6d  %-9d  %s", label, r.num_candidates, top_score, best);
        if (r.decoded) printf("  → \"%s\"", r.text);
        printf("\n");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LDPC iteration sweep: probe how many iterations are needed
// ─────────────────────────────────────────────────────────────────────────────
static void ldpc_sweep(const float* samples, int n_samples, int fs,
                        ftx_protocol_t proto)
{
    printf("\n── LDPC iteration sweep (f=200–2900 Hz) ──\n");
    printf("  %-10s  %-6s  %s\n", "Max iter", "Cands", "Decoded?");
    printf("  %-10s  %-6s  %s\n", "────────", "─────", "───────");

    const int iters[] = { 1, 2, 4, 8, 16, 25, 50 };
    for (int ti = 0; ti < (int)(sizeof(iters)/sizeof(iters[0])); ti++) {
        decode_clear_hashes();
        DiagResult r = decode_detailed(samples, n_samples, fs, proto,
                                        200.0f, 2900.0f, 2, 1, iters[ti]);
        printf("  %-10d  %-6d  %s", iters[ti], r.num_candidates,
               r.decoded ? r.text : (r.num_candidates > 0 ? "candidates but no decode" : "no candidates"));
        if (r.num_candidates > 0)
            printf(" (best score=%d ldpc=%d crc=%s)",
                   r.cands[0].score, r.cands[0].ldpc_errors,
                   r.cands[0].crc_ok ? "OK" : "FAIL");
        printf("\n");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Resample 12→6 kHz and retry (simple 2:1 decimate)
// ─────────────────────────────────────────────────────────────────────────────
static void try_resampled(const std::vector<float>& samples, int fs,
                           ftx_protocol_t proto, const char* wav_path)
{
    if (fs != 12000) { printf("\n  (Resample only needed for 12 kHz input)\n"); return; }

    printf("\n── Resampled 12→6 kHz attempt ──\n");
    std::vector<float> s6k = decimate2(samples);
    int fs6k = fs / 2;

    decode_clear_hashes();
    DiagResult r = decode_detailed(s6k.data(), (int)s6k.size(), fs6k, proto,
                                    200.0f, 2900.0f, 2, 1, 25);
    printf("  Candidates: %d", r.num_candidates);
    if (r.decoded) {
        printf("  → DECODED: \"%s\"\n", r.text);
    } else if (r.num_candidates > 0) {
        printf("  — top score=%d, freq=%.0f Hz, ldpc=%d, crc=%s\n",
               r.cands[0].score, (double)r.cands[0].freq_hz,
               r.cands[0].ldpc_errors, r.cands[0].crc_ok ? "OK" : "FAIL");
    } else {
        printf("  — no candidates at 6 kHz either\n");
    }

    // Write the resampled WAV for offline inspection in Audacity / jt9
    char out_path[512];
    snprintf(out_path, sizeof(out_path), "%s.6k.wav", wav_path);
    if (write_wav(out_path, s6k.data(), (int)s6k.size(), fs6k) == 0)
        printf("  Wrote: %s\n", out_path);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.wav> [expected_message] [ft8|ft4]\n", argv[0]);
        fprintf(stderr, "Example: %s golden/ft4_cq_w1xyz_fn42.wav \"CQ W1XYZ FN42\" ft4\n", argv[0]);
        return 1;
    }

    const char* wav_path     = argv[1];
    const char* expected_msg = (argc >= 3) ? argv[2] : "";
    ftx_protocol_t proto     = FTX_PROTOCOL_FT4;
    if (argc >= 4 && strcmp(argv[3], "ft8") == 0) proto = FTX_PROTOCOL_FT8;

    printf("WAV Diagnostic Tool\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("File:     %s\n", wav_path);
    printf("Protocol: %s\n", proto == FTX_PROTOCOL_FT4 ? "FT4" : "FT8");
    if (*expected_msg) printf("Expected: \"%s\"\n", expected_msg);
    printf("\n");

    // ── Load WAV ──────────────────────────────────────────────────────────
    std::vector<float> samples;
    int fs = 0;
    if (read_wav(wav_path, samples, &fs) != 0) {
        fprintf(stderr, "FATAL: cannot read WAV\n");
        return 2;
    }

    // ── Signal statistics ─────────────────────────────────────────────────
    SigStats st = compute_stats(samples.data(), (int)samples.size(), fs);
    printf("── Signal statistics ──\n");
    printf("  Sample rate:  %d Hz\n", fs);
    printf("  Duration:     %.3f s  (%d samples)\n", (double)st.duration_s, st.n_samples);
    printf("  Peak:         %.4f  (%.1f dBFS)\n",
           (double)st.peak,
           (double)(20.0f * log10f(st.peak + 1e-12f)));
    printf("  RMS:          %.4f  (%.1f dBFS)\n",
           (double)st.rms,
           (double)(20.0f * log10f(st.rms + 1e-12f)));
    printf("  Clipped:      %d samples\n", st.clipped);

    float sym_period = (proto == FTX_PROTOCOL_FT4) ? FT4_SYMBOL_PERIOD : FT8_SYMBOL_PERIOD;
    int   n_syms     = (proto == FTX_PROTOCOL_FT4) ? FT4_NN : FT8_NN;
    float expected_s = n_syms * sym_period;
    printf("  Expected duration (no padding): %.3f s (%d × %.0f ms)\n",
           (double)expected_s, n_syms, (double)(sym_period * 1000.0f));

    float signal_rms_db = 20.0f * log10f(st.rms + 1e-12f);
    printf("\n  NOTE: For FT4 the signal occupies %.0f ms of a %.0f ms slot.\n",
           (double)(expected_s * 1000.0f),
           (double)((proto == FTX_PROTOCOL_FT4) ? 7500.0f : 15000.0f));
    if (st.peak < 0.01f)
        printf("  ⚠ VERY LOW AMPLITUDE — possible silence or wrong channel\n");
    if (st.clipped > 0)
        printf("  ⚠ CLIPPING DETECTED — recording may be distorted\n");

    // ── Full-detail decode at native sample rate ──────────────────────────
    printf("\n── Full decode (native %d Hz, f=200–2900 Hz, time_osr=2, freq_osr=1) ──\n", fs);
    decode_clear_hashes();
    DiagResult r0 = decode_detailed(samples.data(), (int)samples.size(), fs, proto,
                                     200.0f, 2900.0f, 2, 1, 25);
    printf("  Candidates found: %d\n", r0.num_candidates);
    if (r0.num_candidates == 0) {
        printf("  ⚠ No Costas sync found at all — signal is absent, badly timed,\n"
               "    or at a frequency outside 200–2900 Hz.\n");
    }
    for (int i = 0; i < r0.num_candidates; i++) {
        const DiagResult::Cand& c = r0.cands[i];
        // Only show CRC status when LDPC converged (ldpc_errors==0),
        // otherwise both CRC fields are zero-initialized and "OK" is misleading.
        const char* crc_str = c.ldpc_errors == 0
            ? (c.crc_ok ? "OK" : "FAIL")
            : "(n/a)";
        printf("  [%2d] score=%-4d  t=%-3d  freq=%.0f Hz  "
               "ldpc_err=%-3d  crc=%-5s  %s\n",
               i, c.score, c.time_offset, (double)c.freq_hz,
               c.ldpc_errors, crc_str,
               c.decoded ? "DECODED" : "");
    }
    if (r0.decoded) {
        printf("\n  ✓ DECODED: \"%s\"\n", r0.text);
        if (*expected_msg) {
            char norm_exp[256];
            normalize_text(expected_msg, norm_exp, sizeof(norm_exp));
            printf("  Expected: \"%s\"\n", norm_exp);
            printf("  Match:    %s\n\n", strcmp(r0.text, norm_exp) == 0 ? "YES ✓" : "NO ✗");
        }
    }

    // ── Frequency sweep ───────────────────────────────────────────────────
    frequency_sweep(samples.data(), (int)samples.size(), fs, proto, 25);

    // ── LDPC iteration sweep ──────────────────────────────────────────────
    if (r0.num_candidates > 0)
        ldpc_sweep(samples.data(), (int)samples.size(), fs, proto);

    // ── Symbol-period probe ───────────────────────────────────────────────
    // Use frequency range around expected tone cluster (1500±250 Hz)
    probe_symbol_period(samples.data(), (int)samples.size(), fs,
                        1500.0f, 500.0f);

    // ── Resampled attempt (if 12 kHz) ────────────────────────────────────
    try_resampled(samples, fs, proto, wav_path);

    // ── Diagnosis summary ─────────────────────────────────────────────────
    printf("\n═══════════════════════════════════════════════════════════════════\n");
    printf("DIAGNOSIS\n");
    if (r0.decoded) {
        printf("  Result: PASS — decoded successfully at native sample rate.\n");
    } else if (r0.num_candidates > 0) {
        int best_score = r0.cands[0].score;
        int best_ldpc  = r0.cands[0].ldpc_errors;
        bool best_crc  = r0.cands[0].crc_ok;
        printf("  Result: CANDIDATES FOUND but no decode.\n");
        printf("  Best candidate: score=%d, ldpc_errors=%d, crc=%s\n",
               best_score, best_ldpc, best_crc ? "OK" : "FAIL");
        if (best_ldpc > 0 && !best_crc)
            printf("  → LDPC did not converge + CRC failure: signal likely has\n"
                   "    symbol-period error (47ms vs 48ms firmware bug) or\n"
                   "    strong frequency error. Check the period probe results.\n");
        else if (best_ldpc == 0 && !best_crc)
            printf("  → LDPC converged but CRC fails: possible bit errors from\n"
                   "    fractional timing misalignment (known 47ms sym_ms bug).\n");
        else if (best_ldpc > 0 && best_crc)
            printf("  → CRC matches but LDPC errors remain: marginal decode,\n"
                   "    increase max_ldpc_iter or check frequency alignment.\n");
    } else {
        printf("  Result: NO CANDIDATES — Costas sync not found.\n");
        printf("  Possible causes:\n"
               "    1. Signal absent or below noise floor\n"
               "    2. Frequency offset > ±1 bin from 200–2900 Hz range\n"
               "    3. Symbol timing severely wrong (e.g. wrong protocol)\n"
               "    4. Recording made with pre-fix firmware (47ms symbols):\n"
               "       complete Costas block correlation may be below threshold\n");
    }
    printf("\n  See resampled .6k.wav for inspection in Audacity or WSJT-X jt9.\n");
    printf("═══════════════════════════════════════════════════════════════════\n");

    return r0.decoded ? 0 : 1;
}
