#include "decode_helper.h"
#include "../../components/ft8_lib/vendor/ft8/decode.h"
#include "../../components/ft8_lib/common/monitor.h"
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <vector>
#include <map>
#include <string>

// Hash table stub: stores hashes recorded during encode, looks them up during decode
static std::map<uint32_t, std::string> g_hash_table;

static bool hash_lookup(ftx_callsign_hash_type_t hash_type, uint32_t hash, char* callsign) {
    auto it = g_hash_table.find(hash);
    if (it != g_hash_table.end()) {
        strncpy(callsign, it->second.c_str(), 11);
        return true;
    }
    return false;
}

static void hash_save(const char* callsign, uint32_t n22) {
    g_hash_table[n22] = std::string(callsign);
}

static ftx_callsign_hash_interface_t g_hash_if = {
    .lookup_hash = hash_lookup,
    .save_hash = hash_save
};

ftx_callsign_hash_interface_t* decode_get_hash_if(void) {
    return &g_hash_if;
}

void decode_clear_hashes(void) {
    g_hash_table.clear();
}

// ---------------------------------------------------------------------------
// WAV writer
// ---------------------------------------------------------------------------
// Writes a minimal RIFF/WAVE file: PCM, 16-bit, mono.
// The float samples are clamped to [-1, 1] then scaled to int16.
int write_wav(const char* path, const float* samples, int n_samples, int fs)
{
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "write_wav: cannot open '%s'\n", path);
        return -1;
    }

    // Fixed-layout WAV header — all fields little-endian
    const uint16_t num_channels    = 1;
    const uint16_t bits_per_sample = 16;
    const uint32_t byte_rate       = (uint32_t)fs * num_channels * bits_per_sample / 8;
    const uint16_t block_align     = num_channels * bits_per_sample / 8;
    const uint32_t data_bytes      = (uint32_t)n_samples * block_align;
    const uint32_t chunk_size      = 36 + data_bytes;  // RIFF chunk size
    const uint32_t sample_rate_u32 = (uint32_t)fs;

    // RIFF header
    fwrite("RIFF", 1, 4, f);
    fwrite(&chunk_size,      4, 1, f);
    fwrite("WAVE", 1, 4, f);
    // fmt sub-chunk
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16; fwrite(&fmt_size,      4, 1, f);
    uint16_t pcm_fmt  =  1; fwrite(&pcm_fmt,       2, 1, f);
                             fwrite(&num_channels,   2, 1, f);
                             fwrite(&sample_rate_u32,4, 1, f);
                             fwrite(&byte_rate,      4, 1, f);
                             fwrite(&block_align,    2, 1, f);
                             fwrite(&bits_per_sample,2, 1, f);
    // data sub-chunk header
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);

    // Sample data
    for (int i = 0; i < n_samples; i++) {
        float s = samples[i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t v = (int16_t)(s * 32767.0f);
        fwrite(&v, 2, 1, f);
    }

    fclose(f);
    return 0;
}

// ---------------------------------------------------------------------------
// WAV reader
// ---------------------------------------------------------------------------
// Reads a 16-bit mono PCM WAV.  Chunk-based so it tolerates extra metadata
// chunks (LIST, INFO, etc.) inserted by some tools.
// Returns 0 on success, -1 on any parse or format error.
int read_wav(const char* path, std::vector<float>& out_samples, int* out_fs)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "read_wav: cannot open '%s'\n", path);
        return -1;
    }

    // ── RIFF header ─────────────────────────────────────────────────────
    char riff_id[4];
    uint32_t riff_size;
    char wave_id[4];
    if (fread(riff_id, 1, 4, f) != 4 || fread(&riff_size, 4, 1, f) != 1 ||
        fread(wave_id, 1, 4, f) != 4) {
        fprintf(stderr, "read_wav: '%s': truncated RIFF header\n", path);
        fclose(f); return -1;
    }
    if (memcmp(riff_id, "RIFF", 4) || memcmp(wave_id, "WAVE", 4)) {
        fprintf(stderr, "read_wav: '%s': not a RIFF/WAVE file\n", path);
        fclose(f); return -1;
    }

    // ── Walk chunks until we have both fmt and data ──────────────────────
    uint16_t audio_format = 0, num_channels = 0, bits_per_sample = 0;
    uint32_t sample_rate = 0;
    bool have_fmt = false, have_data = false;

    while (!have_data) {
        char chunk_id[4];
        uint32_t chunk_size;
        if (fread(chunk_id, 1, 4, f) != 4 || fread(&chunk_size, 4, 1, f) != 1)
            break;  // EOF or error → checked below

        if (!memcmp(chunk_id, "fmt ", 4)) {
            // Minimum fmt chunk is 16 bytes (PCM)
            if (chunk_size < 16) {
                fprintf(stderr, "read_wav: '%s': fmt chunk too small\n", path);
                fclose(f); return -1;
            }
            fread(&audio_format,   2, 1, f);
            fread(&num_channels,   2, 1, f);
            fread(&sample_rate,    4, 1, f);
            fseek(f, 4 + 2, SEEK_CUR);  // skip byte_rate + block_align
            fread(&bits_per_sample, 2, 1, f);
            if (chunk_size > 16) fseek(f, chunk_size - 16, SEEK_CUR);
            have_fmt = true;

            if (audio_format != 1) {
                fprintf(stderr, "read_wav: '%s': not PCM (format=%u)\n",
                        path, audio_format);
                fclose(f); return -1;
            }
            if (num_channels != 1) {
                fprintf(stderr, "read_wav: '%s': not mono (%u channels)\n",
                        path, num_channels);
                fclose(f); return -1;
            }
            if (bits_per_sample != 16) {
                fprintf(stderr, "read_wav: '%s': not 16-bit (%u bps)\n",
                        path, bits_per_sample);
                fclose(f); return -1;
            }
        } else if (!memcmp(chunk_id, "data", 4)) {
            if (!have_fmt) {
                fprintf(stderr, "read_wav: '%s': data before fmt\n", path);
                fclose(f); return -1;
            }
            uint32_t n_samples = chunk_size / 2;  // 2 bytes per int16
            out_samples.resize(n_samples);
            for (uint32_t i = 0; i < n_samples; i++) {
                int16_t v;
                if (fread(&v, 2, 1, f) != 1) {
                    fprintf(stderr, "read_wav: '%s': truncated data\n", path);
                    fclose(f); return -1;
                }
                out_samples[i] = v / 32768.0f;
            }
            have_data = true;
        } else {
            // Unknown chunk — skip it (pad to even size per RIFF spec)
            uint32_t skip = chunk_size + (chunk_size & 1);
            fseek(f, skip, SEEK_CUR);
        }
    }
    fclose(f);

    if (!have_fmt || !have_data) {
        fprintf(stderr, "read_wav: '%s': missing fmt or data chunk\n", path);
        return -1;
    }
    *out_fs = (int)sample_rate;
    return 0;
}

void normalize_text(const char* text, char* out_norm, int out_len) {
    if (!text || !out_norm || out_len <= 0) return;

    char* dst = out_norm;
    char* end = out_norm + out_len - 1;  // leave room for null terminator
    bool prev_space = true;

    for (const char* src = text; *src && dst < end; src++) {
        char c = *src;

        // Convert to uppercase
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }

        // Skip leading whitespace
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!prev_space && dst > out_norm) {
                *dst++ = ' ';
                prev_space = true;
            }
        } else {
            *dst++ = c;
            prev_space = false;
        }
    }

    // Strip trailing whitespace
    while (dst > out_norm && (*(dst - 1) == ' ' || *(dst - 1) == '\t')) {
        dst--;
    }

    *dst = '\0';
}

DecodeResult decode_pcm(const float* samples, int n_samples, int fs,
                        ftx_protocol_t proto) {
    DecodeResult result = {false, "", 0.0f};

    // Use firmware-compatible settings to fit monitor.c's static buffer (MONITOR_NFFT_MAX=960)
    // At 6kHz with freq_osr=1: nfft = 960, matches static allocation exactly
    monitor_config_t cfg = {
        .f_min = 200.0f,
        .f_max = 2900.0f,
        .sample_rate = fs,        // Use input sample rate (test provides 6kHz)
        .time_osr = 2,            // Firmware default
        .freq_osr = 1,            // Critical: >= 2 would exceed MONITOR_NFFT_MAX (960)
        .protocol = proto
    };

    monitor_t mon;
    fprintf(stderr, "calling monitor_init...\n");
    fflush(stderr);
    monitor_init(&mon, &cfg);
    fprintf(stderr, "calling monitor_reset...\n");
    fflush(stderr);
    monitor_reset(&mon);

    // Process audio in blocks
    int block_size = mon.block_size;
    for (int i = 0; i + block_size <= n_samples; i += block_size) {
        monitor_process(&mon, samples + i);
    }

    // Find candidates
    ftx_candidate_t candidates[20];
    int num_candidates = ftx_find_candidates(&mon.wf, 20, candidates, 0);

    // Try to decode each candidate
    for (int i = 0; i < num_candidates; i++) {
        ftx_message_t msg;
        ftx_decode_status_t status;

        if (ftx_decode_candidate(&mon.wf, &candidates[i], 4, &msg, &status)) {
            // Decode succeeded!
            char text[256];
            ftx_message_offsets_t offsets = {};
            ftx_message_decode(&msg, &g_hash_if, text, &offsets);

            // Normalize the decoded text
            normalize_text(text, result.text, sizeof(result.text));
            result.found = true;
            result.snr = candidates[i].score;
            break;  // Return first successful decode
        }
    }

    monitor_free(&mon);
    return result;
}
