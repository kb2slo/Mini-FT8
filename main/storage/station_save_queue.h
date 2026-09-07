#pragma once

#include <string>

// Last-write-wins Station.txt blob. No ESP-IDF. The caller starts at most one
// write; later submits replace the pending blob until that write completes.

struct StationSaveQueue {
    bool in_flight = false;
    bool has_pending = false;
    std::string pending;
};

void station_save_queue_init(StationSaveQueue* q);

// Offer a serialized Station.txt. If a write can start now, moves the blob to
// *out_start and returns true. If a write is already in flight, stores it as
// pending (replacing any older pending blob) and returns false.
bool station_save_queue_submit(StationSaveQueue* q, std::string blob, std::string* out_start);

// A write finished. If another blob is pending, moves it to *out_next and
// returns true (start that write). Otherwise the queue is idle and returns false.
bool station_save_queue_complete(StationSaveQueue* q, std::string* out_next);

bool station_save_queue_busy(const StationSaveQueue* q);
