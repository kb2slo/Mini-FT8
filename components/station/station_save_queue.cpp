#include "station_save_queue.h"

#include <utility>

void station_save_queue_init(StationSaveQueue* q)
{
    q->in_flight = false;
    q->has_pending = false;
    q->pending.clear();
}

bool station_save_queue_submit(StationSaveQueue* q, std::string blob, std::string* out_start)
{
    if (q->in_flight) {
        q->pending = std::move(blob);
        q->has_pending = true;
        return false;
    }
    q->in_flight = true;
    q->has_pending = false;
    q->pending.clear();
    if (out_start != nullptr) {
        *out_start = std::move(blob);
    }
    return true;
}

bool station_save_queue_complete(StationSaveQueue* q, std::string* out_next)
{
    q->in_flight = false;
    if (!q->has_pending) {
        q->pending.clear();
        return false;
    }
    q->has_pending = false;
    q->in_flight = true;
    if (out_next != nullptr) {
        *out_next = std::move(q->pending);
    } else {
        q->pending.clear();
    }
    return true;
}

bool station_save_queue_busy(const StationSaveQueue* q)
{
    return q->in_flight || q->has_pending;
}
