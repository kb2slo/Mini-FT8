#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Decimation factor: 48kHz -> 6kHz
#define RESAMPLE_FACTOR 8

// Resampler state (minimal - no filter history needed)
typedef struct {
    int _unused;  // Placeholder for API compatibility
} resample_state_t;

// Initialize resampler state (no-op, kept for API compatibility)
void resample_init(resample_state_t* state);


// Decimate 48kHz mono float samples to 6kHz (simple decimation, no filtering)
// Input: 48kHz mono float samples
// Output: 6kHz mono float samples
// Returns: Number of output samples written (in_samples / RESAMPLE_FACTOR)
// Note: in_samples must be a multiple of RESAMPLE_FACTOR
int resample_48k_to_6k(
    resample_state_t* state,
    const float* in,        // Input buffer (48kHz mono)
    float* out,             // Output buffer (12kHz mono)
    int in_samples          // Number of input samples
);


// Generic conversion: 48kHz PCM (16/24-bit mono/stereo) -> 6kHz mono float
// Input: Raw PCM USB audio bytes
// Output: 6kHz mono float samples ready for FT8 processing
// Returns: Number of 6kHz samples written
int uac_pcm_to_ft8_samples(
    resample_state_t* state,
    const uint8_t* in,      // USB PCM buffer
    int in_bytes,           // Number of input bytes
    float* out,             // Output buffer (12kHz mono float)
    int bit_resolution,     // 16 or 24
    int channels            // 1 or 2
);

#ifdef __cplusplus
}
#endif
