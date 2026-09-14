#pragma once

// Generated seed table (see main/CMakeLists.txt). web/ is the source of truth;
// this header is only the C shape of that table.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;   // path under the web FS root, e.g. "pairing.js"
    const char *embed;  // NUL-terminated EMBED_TXTFILES blob
    size_t size;        // on-disk size (embed without the trailing NUL)
} web_seed_file_t;

extern const web_seed_file_t web_seed_files[];
extern const size_t web_seed_files_count;

#ifdef __cplusplus
}
#endif
