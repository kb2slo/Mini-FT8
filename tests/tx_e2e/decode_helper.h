#ifndef _TX_E2E_DECODE_HELPER_H_
#define _TX_E2E_DECODE_HELPER_H_

#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
#include <vector>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "../../components/ft8_lib/vendor/ft8/constants.h"
#include "../../components/ft8_lib/vendor/ft8/message.h"

typedef struct {
    bool found;
    char text[256];
    float snr;
} DecodeResult;

/// Get the hash interface used by decoder (for test synchronization)
ftx_callsign_hash_interface_t* decode_get_hash_if(void);

/// Clear the hash table (call between test cases)
void decode_clear_hashes(void);

/// Decode PCM samples using ft8_lib reference decoder
///
/// @param samples     PCM samples (float, -1.0 to 1.0 range)
/// @param n_samples   Number of samples
/// @param fs          Sample rate in Hertz
/// @param proto       Protocol: FTX_PROTOCOL_FT8 or FTX_PROTOCOL_FT4
/// @return DecodeResult with .found=true if successful
DecodeResult decode_pcm(const float* samples, int n_samples, int fs,
                        ftx_protocol_t proto);

/// Read a 16-bit mono PCM WAV file into a float sample vector.
/// Validates format (must be PCM, mono, 16-bit).
///
/// @param path        Input file path
/// @param out_samples [OUT] float samples, range [-1.0, 1.0]
/// @param out_fs      [OUT] sample rate found in the file
/// @return 0 on success, -1 on error (message printed to stderr)
#ifdef __cplusplus
int read_wav(const char* path, std::vector<float>& out_samples, int* out_fs);
#endif

/// Write PCM samples to a 16-bit mono WAV file.
/// Useful for dumping failure artifacts for inspection in Audacity / WSJT-X.
///
/// @param path        Output file path (e.g. "fail_case3.wav")
/// @param samples     Float PCM, range [-1.0, 1.0]
/// @param n_samples   Number of samples
/// @param fs          Sample rate in Hertz
/// @return 0 on success, -1 on error (message printed to stderr)
int write_wav(const char* path, const float* samples, int n_samples, int fs);

/// Normalize message text for comparison
/// - Convert to uppercase
/// - Collapse whitespace
/// - Strip leading/trailing whitespace
/// - Handle <HASH> callsign notation
///
/// @param text        Input text
/// @param out_norm    Output buffer for normalized text (pre-allocated)
/// @param out_len     Size of output buffer
void normalize_text(const char* text, char* out_norm, int out_len);

#ifdef __cplusplus
}
#endif

#endif // _TX_E2E_DECODE_HELPER_H_
