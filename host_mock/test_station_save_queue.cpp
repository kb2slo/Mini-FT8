#include <cstdio>
#include <string>

#include "station_save_queue.h"

static int g_fails = 0;

static void fail(const char* msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    ++g_fails;
}

static void expect_true(bool cond, const char* msg)
{
    if (!cond) {
        fail(msg);
    }
}

static void expect_str(const std::string& got, const char* want, const char* msg)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got '%s' want '%s')\n", msg, got.c_str(), want);
        ++g_fails;
    }
}

int main()
{
    {
        StationSaveQueue q;
        station_save_queue_init(&q);
        expect_true(!station_save_queue_busy(&q), "fresh idle");

        std::string start;
        expect_true(station_save_queue_submit(&q, std::string("one"), &start), "idle starts write");
        expect_str(start, "one", "start blob");
        expect_true(station_save_queue_busy(&q), "in flight is busy");
        expect_true(!q.has_pending, "no pending on first submit");
    }

    {
        StationSaveQueue q;
        station_save_queue_init(&q);
        std::string start;
        station_save_queue_submit(&q, std::string("a"), &start);

        std::string untouched = "keep";
        expect_true(!station_save_queue_submit(&q, std::string("b"), &untouched), "second is pending");
        expect_str(untouched, "keep", "out_start unchanged when pending");
        expect_str(q.pending, "b", "pending is second blob");

        expect_true(!station_save_queue_submit(&q, std::string("c"), &untouched), "third replaces pending");
        expect_str(q.pending, "c", "last write wins");

        std::string next;
        expect_true(station_save_queue_complete(&q, &next), "complete starts pending");
        expect_str(next, "c", "next is last pending not b");
        expect_true(station_save_queue_busy(&q), "second write in flight");
        expect_true(!q.has_pending, "pending consumed");

        expect_true(!station_save_queue_complete(&q, &next), "complete goes idle");
        expect_true(!station_save_queue_busy(&q), "idle after last complete");
    }

    {
        StationSaveQueue q;
        station_save_queue_init(&q);
        std::string next;
        expect_true(!station_save_queue_complete(&q, &next), "complete on idle");
        expect_true(!station_save_queue_busy(&q), "still idle");
    }

    if (g_fails != 0) {
        fprintf(stderr, "%d failures\n", g_fails);
        return 1;
    }
    printf("PASS: station save queue\n");
    return 0;
}
