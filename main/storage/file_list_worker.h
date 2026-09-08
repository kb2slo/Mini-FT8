#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "file_list.h"

// Low-priority FATFS directory list. Last request wins while a listing is in
// flight. Completion is drained on the app loop — not invoked from the worker.

struct FileListDone {
    FileListKind kind = FileListKind::QsoDaily;
    uint32_t gen = 0;
    FileListFail fail = FileListFail::None;
    std::vector<std::string> names;
};

void file_list_worker_init();
uint32_t file_list_worker_submit(FileListKind kind);
bool file_list_worker_take(FileListDone* out);
void file_list_worker_flush();
