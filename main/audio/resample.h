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

// Convert 24-bit stereo samples to mono float
// Input: 24-bit LE stereo samples (6 bytes per sample pair: L0 L1 L2 R0 R1 R2)
// Output: Mono float samples in range [-1.0, 1.0]
// Returns: Number of mono samples written
int convert_24bit_stereo_to_mono_float(
    const uint8_t* in,      // Input buffer (24-bit stereo, little-endian)
    float* out,             // Output buffer (mono float)
    int num_stereo_samples  // Number of stereo sample pairs
);

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

// Combined conversion: 24-bit/48kHz/stereo -> 12kHz mono float
// This is the main entry point for UAC audio processing
// Input: Raw USB audio data (24-bit LE stereo at 48kHz)
// Output: 12kHz mono float samples ready for FT8 processing
// Returns: Number of 12kHz samples written
int uac_to_ft8_samples(
    resample_state_t* state,
    const uint8_t* in,      // USB audio buffer (24-bit stereo)
    float* out,             // Output buffer (12kHz mono float)
    int num_stereo_samples  // Number of stereo sample pairs at 48kHz
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
