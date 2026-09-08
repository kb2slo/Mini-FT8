#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_SOURCE_QMX_UAC = 0,
} audio_source_backend_t;

void audio_source_set_backend(audio_source_backend_t backend);
audio_source_backend_t audio_source_get_backend(void);
const char* audio_source_backend_name(audio_source_backend_t backend);

bool audio_source_start(void);
void audio_source_stop(void);

bool audio_source_is_streaming(void);

#ifdef __cplusplus
}
#endif
