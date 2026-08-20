#include "file_list_queue.h"

void file_list_queue_init(FileListQueue* q)
{
    q->in_flight = false;
    q->has_pending = false;
    q->pending_kind = FileListKind::QsoDaily;
    q->pending_gen = 0;
    q->in_flight_kind = FileListKind::QsoDaily;
    q->in_flight_gen = 0;
    q->next_gen = 1;
}

bool file_list_queue_submit(FileListQueue* q,
                            FileListKind kind,
                            uint32_t* out_gen,
                            FileListKind* out_start)
{
    const uint32_t gen = q->next_gen++;
    if (q->next_gen == 0) {
        q->next_gen = 1;
    }
    if (out_gen != nullptr) {
        *out_gen = gen;
    }
    if (q->in_flight) {
        q->pending_kind = kind;
        q->pending_gen = gen;
        q->has_pending = true;
        return false;
    }
    q->in_flight = true;
    q->has_pending = false;
    q->in_flight_kind = kind;
    q->in_flight_gen = gen;
    if (out_start != nullptr) {
        *out_start = kind;
    }
    return true;
}

bool file_list_queue_complete(FileListQueue* q,
                              FileListKind* out_next,
                              uint32_t* out_next_gen)
{
    q->in_flight = false;
    if (!q->has_pending) {
        return false;
    }
    q->has_pending = false;
    q->in_flight = true;
    q->in_flight_kind = q->pending_kind;
    q->in_flight_gen = q->pending_gen;
    if (out_next != nullptr) {
        *out_next = q->in_flight_kind;
    }
    if (out_next_gen != nullptr) {
        *out_next_gen = q->in_flight_gen;
    }
    return true;
}

bool file_list_queue_busy(const FileListQueue* q)
{
    return q->in_flight || q->has_pending;
}
