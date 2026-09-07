#include "resample.h"
#include <string.h>

void resample_init(resample_state_t* state) {
    // No-op - no filter state to initialize
    (void)state;
}

int convert_24bit_stereo_to_mono_float(
    const uint8_t* in,
    float* out,
    int num_stereo_samples
) {
    const float scale = 1.0f / 8388608.0f;  // 2^23 for 24-bit normalization

    for (int i = 0; i < num_stereo_samples; i++) {
        // Read left channel (24-bit LE)
        int offset = i * 6;  // 6 bytes per stereo sample (3+3)
        int32_t left = in[offset] | (in[offset + 1] << 8) | (in[offset + 2] << 16);
        // Sign extend from 24-bit to 32-bit
        if (left & 0x800000) {
            left |= 0xFF000000;
        }

        // Read right channel (24-bit LE)
        int32_t right = in[offset + 3] | (in[offset + 4] << 8) | (in[offset + 5] << 16);
        // Sign extend from 24-bit to 32-bit
        if (right & 0x800000) {
            right |= 0xFF000000;
        }

        // Downmix to mono: (L + R) / 2
        float mono = ((float)left + (float)right) * 0.5f * scale;
        out[i] = mono;
    }

    return num_stereo_samples;
}

int resample_48k_to_6k(
    resample_state_t* state,
    const float* in,
    float* out,
    int in_samples
) {
    (void)state;  // Unused - no filter state needed

    int out_samples = in_samples / RESAMPLE_FACTOR;

    // Simple decimation: take every RESAMPLE_FACTOR-th sample
    // No anti-aliasing filter needed - input is already bandwidth-limited
    for (int i = 0; i < out_samples; i++) {
        out[i] = in[i * RESAMPLE_FACTOR];
    }

    return out_samples;
}

int uac_to_ft8_samples(
    resample_state_t* state,
    const uint8_t* in,
    float* out,
    int num_stereo_samples
) {
    return uac_pcm_to_ft8_samples(state, in, num_stereo_samples * 6, out, 24, 2);
}

int uac_pcm_to_ft8_samples(
    resample_state_t* state,
    const uint8_t* in,
    int in_bytes,
    float* out,
    int bit_resolution,
    int channels
) {
    if (!state || !in || !out) return 0;
    if (!((bit_resolution == 16) || (bit_resolution == 24))) return 0;
    if (!((channels == 1) || (channels == 2))) return 0;

    const int bytes_per_sample = bit_resolution / 8;
    const int frame_bytes = bytes_per_sample * channels;
    if (frame_bytes <= 0) return 0;

    int total_frames = in_bytes / frame_bytes;
    if (total_frames <= 0) return 0;

    // USB UAC delivers 768 stereo samples per transfer (4608 bytes / 6 bytes).
    // 1024 samples gives comfortable headroom without wasting BSS.
    static float temp_mono[1024];
    int total_out = 0;
    const uint8_t* in_ptr = in;
    float* out_ptr = out;
    int remaining = total_frames;

    while (remaining > 0) {
        int chunk = (remaining > 1024) ? 1024 : remaining;

        for (int i = 0; i < chunk; ++i) {
            const uint8_t* frame = in_ptr + i * frame_bytes;
            float mono = 0.0f;

            if (bit_resolution == 24) {
                for (int ch = 0; ch < channels; ++ch) {
                    const uint8_t* p = frame + ch * 3;
                    int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
                    if (v & 0x800000) v |= 0xFF000000;
                    mono += (float)v / 8388608.0f;
                }
            } else { // 16-bit
                for (int ch = 0; ch < channels; ++ch) {
                    const uint8_t* p = frame + ch * 2;
                    int16_t v = (int16_t)(p[0] | (p[1] << 8));
                    mono += (float)v / 32768.0f;
                }
            }

            temp_mono[i] = mono / (float)channels;
        }

        int out_count = resample_48k_to_6k(state, temp_mono, out_ptr, chunk);
        in_ptr += chunk * frame_bytes;
        out_ptr += out_count;
        total_out += out_count;
        remaining -= chunk;
    }

    return total_out;
}
