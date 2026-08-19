#pragma once

#include <string>
#include <vector>

// QSO-screen list/parse. No ESP-IDF. Host tests and firmware both link this.
// FATFS stays in the caller.

struct QsoBrowseBand {
    const char* name;
    float freq_khz;
};

struct QsoLogEntry {
    std::string time_on;
    std::string band;
    std::string call;
    bool has_rst_rcvd = false;
    int rst_rcvd = 0;
    bool has_rst_sent = false;
    int rst_sent = 0;
};

enum class QsoBrowsePageView { Default, Alternate };

struct QsoBrowsePager {
    int skip = 0;
    int take = 6;
    int matched = 0;
    bool has_next = false;
    std::vector<QsoLogEntry> entries;
};

bool qso_browse_is_daily_log_name(const std::string& name);

void qso_browse_select_daily_files(const std::vector<std::string>& all_names,
                                   std::vector<std::string>* daily_out);

void qso_browse_fill_file_lines(const std::vector<std::string>& files,
                                std::vector<std::string>* lines);

void qso_browse_pager_reset(QsoBrowsePager* io, int skip, int take);

// Feed one file line. Returns false when the page is full (caller may stop reading).
bool qso_browse_pager_feed(QsoBrowsePager* io,
                           const std::string& line,
                           const QsoBrowseBand* bands,
                           int band_count);

void qso_browse_format_entry_lines(const std::vector<QsoLogEntry>& entries,
                                   QsoBrowsePageView view,
                                   std::vector<std::string>* lines);
