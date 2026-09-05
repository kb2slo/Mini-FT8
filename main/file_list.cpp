#include "file_list.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "qso_browse.h"

const char* file_list_fail_text(FileListFail fail)
{
    switch (fail) {
        case FileListFail::None:
            return nullptr;
        case FileListFail::Busy:
            return "Storage busy";
        case FileListFail::Unavailable:
            return "Storage unavailable";
        case FileListFail::Failed:
            return "List failed";
    }
    return "List failed";
}

namespace {

void apply_qso_daily(FileListFail fail,
                     const std::vector<std::string>& all_names,
                     std::vector<std::string>* files_out,
                     std::vector<std::string>* lines_out)
{
    files_out->clear();
    lines_out->clear();
    if (fail != FileListFail::None) {
        lines_out->push_back(file_list_fail_text(fail));
        return;
    }
    qso_browse_select_daily_files(all_names, files_out);
    qso_browse_fill_file_lines(*files_out, lines_out);
}

void apply_delete(FileListFail fail,
                  const std::vector<std::string>& all_names,
                  std::vector<std::string>* files_out,
                  std::vector<std::string>* lines_out)
{
    files_out->clear();
    lines_out->clear();
    if (fail != FileListFail::None) {
        lines_out->push_back("No storage files");
        return;
    }
    *files_out = all_names;
    std::sort(files_out->begin(), files_out->end(), std::greater<std::string>());
    files_out->erase(std::remove(files_out->begin(), files_out->end(), "Station.txt"),
                     files_out->end());
    if (files_out->empty()) {
        lines_out->push_back("No storage files");
        return;
    }
    for (const std::string& name : *files_out) {
        lines_out->push_back(std::string("DEL ") + name);
    }
}

}  // namespace

void file_list_apply(FileListKind kind,
                     FileListFail fail,
                     const std::vector<std::string>& all_names,
                     std::vector<std::string>* files_out,
                     std::vector<std::string>* lines_out)
{
    switch (kind) {
        case FileListKind::QsoDaily:
            apply_qso_daily(fail, all_names, files_out, lines_out);
            return;
        case FileListKind::Delete:
            apply_delete(fail, all_names, files_out, lines_out);
            return;
    }
}
