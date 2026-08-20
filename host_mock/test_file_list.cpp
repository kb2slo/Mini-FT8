#include <cstdio>
#include <string>
#include <vector>

#include "file_list.h"
#include "file_list_queue.h"

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

static void expect_int(int got, int want, const char* msg)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %d want %d)\n", msg, got, want);
        ++g_fails;
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
        expect_str(file_list_fail_text(FileListFail::Busy), "Storage busy", "busy text");
        expect_str(file_list_fail_text(FileListFail::Unavailable), "Storage unavailable",
                   "unavailable text");
        expect_str(file_list_fail_text(FileListFail::Failed), "List failed", "failed text");
    }

    {
        std::vector<std::string> files;
        std::vector<std::string> lines;
        file_list_apply(FileListKind::QsoDaily, FileListFail::Busy, {"20260819.adi"}, &files,
                        &lines);
        expect_true(files.empty(), "qso fail no files");
        expect_int(static_cast<int>(lines.size()), 1, "qso fail one line");
        expect_str(lines[0], "Storage busy", "qso busy line");
    }

    {
        std::vector<std::string> files;
        std::vector<std::string> lines;
        file_list_apply(FileListKind::QsoDaily, FileListFail::None,
                        {"Station.txt", "20260818.adi", "notes.txt", "20260819.adi"}, &files,
                        &lines);
        expect_int(static_cast<int>(files.size()), 2, "two daily");
        expect_str(files[0], "20260819.adi", "newest first");
        expect_str(lines[0], "20260819.adi", "qso line is name");
    }

    {
        std::vector<std::string> files;
        std::vector<std::string> lines;
        file_list_apply(FileListKind::QsoDaily, FileListFail::None, {"Station.txt"}, &files,
                        &lines);
        expect_true(files.empty(), "no daily");
        expect_str(lines[0], "No YYYYMMDD.adi", "empty daily message");
    }

    {
        std::vector<std::string> files;
        std::vector<std::string> lines;
        file_list_apply(FileListKind::Delete, FileListFail::Busy, {"20260819.adi"}, &files,
                        &lines);
        expect_true(files.empty(), "delete fail no files");
        expect_str(lines[0], "No storage files", "delete fail hides reason");
    }

    {
        std::vector<std::string> files;
        std::vector<std::string> lines;
        file_list_apply(FileListKind::Delete, FileListFail::None,
                        {"a.adi", "Station.txt", "b.adi"}, &files, &lines);
        expect_int(static_cast<int>(files.size()), 2, "station dropped");
        expect_str(files[0], "b.adi", "delete newest first");
        expect_str(files[1], "a.adi", "delete older last");
        expect_str(lines[0], "DEL b.adi", "delete prefix");
        expect_str(lines[1], "DEL a.adi", "delete prefix 2");
    }

    {
        FileListQueue q;
        file_list_queue_init(&q);
        expect_true(!file_list_queue_busy(&q), "fresh idle");

        uint32_t gen = 0;
        FileListKind start = FileListKind::Delete;
        expect_true(file_list_queue_submit(&q, FileListKind::QsoDaily, &gen, &start),
                    "idle starts");
        expect_int(static_cast<int>(gen), 1, "first gen");
        expect_true(start == FileListKind::QsoDaily, "start kind");
        expect_true(file_list_queue_busy(&q), "in flight is busy");
        expect_true(!q.has_pending, "no pending on first");
    }

    {
        FileListQueue q;
        file_list_queue_init(&q);
        uint32_t gen = 0;
        FileListKind start = FileListKind::QsoDaily;
        file_list_queue_submit(&q, FileListKind::QsoDaily, &gen, &start);

        FileListKind untouched = FileListKind::QsoDaily;
        uint32_t gen_b = 0;
        expect_true(!file_list_queue_submit(&q, FileListKind::Delete, &gen_b, &untouched),
                    "second is pending");
        expect_true(untouched == FileListKind::QsoDaily, "out_start unchanged");
        expect_int(static_cast<int>(gen_b), 2, "pending gen");
        expect_true(q.pending_kind == FileListKind::Delete, "pending is delete");

        uint32_t gen_c = 0;
        expect_true(!file_list_queue_submit(&q, FileListKind::QsoDaily, &gen_c, &untouched),
                    "third replaces pending");
        expect_true(q.pending_kind == FileListKind::QsoDaily, "last request wins");
        expect_int(static_cast<int>(gen_c), 3, "third gen");

        FileListKind next = FileListKind::Delete;
        uint32_t next_gen = 0;
        expect_true(file_list_queue_complete(&q, &next, &next_gen), "complete starts pending");
        expect_true(next == FileListKind::QsoDaily, "next is last pending");
        expect_int(static_cast<int>(next_gen), 3, "next gen is third");
        expect_true(file_list_queue_busy(&q), "second list in flight");
        expect_true(!q.has_pending, "pending consumed");

        expect_true(!file_list_queue_complete(&q, &next, &next_gen), "complete goes idle");
        expect_true(!file_list_queue_busy(&q), "idle after last complete");
    }

    if (g_fails != 0) {
        fprintf(stderr, "%d failure(s)\n", g_fails);
        return 1;
    }
    printf("PASS: file list\n");
    return 0;
}
