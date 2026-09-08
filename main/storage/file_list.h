#pragma once

#include <string>
#include <vector>

// Format a storage directory listing for Q (daily logs) or DEBUG delete.
// No ESP-IDF. FATFS stays in the caller.

enum class FileListKind {
    QsoDaily,
    Delete,
};

enum class FileListFail {
    None,
    Busy,
    Unavailable,
    Failed,
};

const char* file_list_fail_text(FileListFail fail);

void file_list_apply(FileListKind kind,
                     FileListFail fail,
                     const std::vector<std::string>& all_names,
                     std::vector<std::string>* files_out,
                     std::vector<std::string>* lines_out);
