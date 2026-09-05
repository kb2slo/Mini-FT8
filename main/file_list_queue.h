#pragma once

#include <cstdint>

#include "file_list.h"

// Last-request-wins directory list. No ESP-IDF. The caller starts at most one
// listing; later submits replace the pending kind until that listing completes.

struct FileListQueue {
    bool in_flight = false;
    bool has_pending = false;
    FileListKind pending_kind = FileListKind::QsoDaily;
    uint32_t pending_gen = 0;
    FileListKind in_flight_kind = FileListKind::QsoDaily;
    uint32_t in_flight_gen = 0;
    uint32_t next_gen = 1;
};

void file_list_queue_init(FileListQueue* q);

// Offer a listing. Always assigns *out_gen. If a listing can start now, sets
// *out_start and returns true. If one is already in flight, stores this request
// as pending (replacing any older pending) and returns false.
bool file_list_queue_submit(FileListQueue* q,
                            FileListKind kind,
                            uint32_t* out_gen,
                            FileListKind* out_start);

// A listing finished. If another request is pending, starts it: sets *out_next
// and *out_next_gen and returns true. Otherwise the queue is idle.
bool file_list_queue_complete(FileListQueue* q,
                              FileListKind* out_next,
                              uint32_t* out_next_gen);

bool file_list_queue_busy(const FileListQueue* q);
