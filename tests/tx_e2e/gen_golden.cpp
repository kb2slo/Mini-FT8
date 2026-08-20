// gen_golden.cpp
// ──────────────────────────────────────────────────────────────────────────────
// Generates the golden WAV files committed under tests/tx_e2e/golden/.
// Run once after any deliberate change to the encoder or synth, then commit
// the resulting WAV files.
//
// Usage:
//   gen_golden [output-dir]   (default: "golden" relative to CWD)
//
// Each WAV is 16-bit PCM mono at 6 kHz — the firmware's native sample rate.
// The encode → synth path is the same as test_l1_encoder (Layer 1, no state
// machine) so the files serve as clean decoder regression anchors.
// ──────────────────────────────────────────────────────────────────────────────

#include <cstdio>
#include <cstring>
#include <vector>
#include "decode_helper.h"
#include "synth.h"
#include "../../components/ft8_lib/vendor/ft8/encode.h"
#include "../../components/ft8_lib/vendor/ft8/message.h"
#include "../../components/ft8_lib/vendor/ft8/constants.h"

struct GoldenSpec {
    const char*    filename;
    const char*    text;
    ftx_protocol_t proto;
    float          base_hz;
};

// This table must stay in sync with test_golden_rx.cpp.
static const GoldenSpec SPECS[] = {
    { "ft8_w1abc_k9xyz_rr73.wav",  "W1ABC K9XYZ RR73", FTX_PROTOCOL_FT8, 1500.0f },
    { "ft8_cq_w1xyz_fn42.wav",     "CQ W1XYZ FN42",    FTX_PROTOCOL_FT8, 1500.0f },
    { "ft8_w1abc_k9xyz_neg12.wav", "W1ABC K9XYZ -12",  FTX_PROTOCOL_FT8, 1500.0f },
    { "ft8_w1abc_k9xyz_fn42.wav",  "W1ABC K9XYZ FN42", FTX_PROTOCOL_FT8, 1500.0f },
    { "ft4_w1abc_k9xyz_rr73.wav",  "W1ABC K9XYZ RR73", FTX_PROTOCOL_FT4, 1500.0f },
    { "ft4_cq_w1xyz_fn42.wav",     "CQ W1XYZ FN42",    FTX_PROTOCOL_FT4, 1500.0f },
    { "ft4_w1abc_k9xyz_neg12.wav", "W1ABC K9XYZ -12",  FTX_PROTOCOL_FT4, 1500.0f },
};
static const int N_SPECS = (int)(sizeof(SPECS) / sizeof(SPECS[0]));

int main(int argc, char** argv)
{
    const char* outdir = (argc > 1) ? argv[1] : "golden";
    const int   FS     = 6000;
    const float AMP    = 0.5f;

    printf("gen_golden: writing %d WAV files to '%s'\n\n", N_SPECS, outdir);

    int ok = 0, fail = 0;
    for (int i = 0; i < N_SPECS; i++) {
        const GoldenSpec& s = SPECS[i];
        printf("  [%d/%d] %s (%s @ %.0f Hz) ... ",
               i + 1, N_SPECS,
               (s.proto == FTX_PROTOCOL_FT8) ? "FT8" : "FT4",
               s.text, s.base_hz);
        fflush(stdout);

        // Encode message → payload
        ftx_message_t msg;
        decode_clear_hashes();
        ftx_message_rc_t rc = ftx_message_encode(&msg, decode_get_hash_if(), s.text);
        if (rc != FTX_MESSAGE_RC_OK) {
            printf("FAIL (encode error %d)\n", rc);
            fail++;
            continue;
        }

        // Payload → tones
        uint8_t tones[FT8_NN > FT4_NN ? FT8_NN : FT4_NN];
        int   nn;
        float sym_period, spacing;
        if (s.proto == FTX_PROTOCOL_FT8) {
            ft8_encode(msg.payload, tones);
            nn = FT8_NN; sym_period = FT8_SYMBOL_PERIOD; spacing = 6.25f;
        } else {
            ft4_encode(msg.payload, tones);
            nn = FT4_NN; sym_period = FT4_SYMBOL_PERIOD; spacing = 20.8333f;
        }

        // Tones → PCM with 0.5 s silence padding on both sides
        int padding       = (int)(0.5f * FS);
        int signal_samps  = (int)(nn * sym_period * FS);
        int total_samps   = padding + signal_samps + padding;
        std::vector<float> pcm(total_samps, 0.0f);
        int synth_len = 0;
        synth_fsk(tones, nn, sym_period, s.base_hz, spacing,
                  FS, AMP, pcm.data() + padding, &synth_len);

        // PCM → WAV
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", outdir, s.filename);
        if (write_wav(path, pcm.data(), total_samps, FS) == 0) {
            printf("OK (%d samples, %.2f s)\n", total_samps, (float)total_samps / FS);
            ok++;
        } else {
            printf("FAIL (write error)\n");
            fail++;
        }
    }

    printf("\ngen_golden: %d/%d files written\n", ok, N_SPECS);
    return (fail > 0) ? 1 : 0;
}
