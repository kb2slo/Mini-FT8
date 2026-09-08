#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Pure ADIF helpers. No ESP-IDF. Host tests and firmware both link this.

struct AdifRecord {
    std::string text;  // record body through <eor>, no trailing newline
    std::string key;   // station_callsign|call|qso_date|time_on (normalized)
};

enum class AdifMergeStatus : unsigned char {
    OK = 0,
    PARSE_ARCHIVE = 1,
    PARSE_INCOMING = 2,
    WRITE_FAILED = 3,
};

struct AdifStream {
    std::string buf;
    bool bom_done = false;
    bool header_done = false;
    bool failed = false;
};

using AdifRecordFn = bool (*)(const AdifRecord& rec, void* ctx);

bool adif_is_adi_filename(const std::string& name_or_path);

// Empty content is a valid 0-record file. Non-ADIF leftover text fails.
bool adif_parse(const std::string& content, std::vector<AdifRecord>& records);

// Incremental parse. `eof` flushes; leftover non-whitespace without `<eor>` fails.
// One in-flight record is buffered (capped). `on_record` false aborts.
bool adif_stream_feed(AdifStream* io, const char* data, size_t len, bool eof,
                      AdifRecordFn on_record, void* ctx);

std::string adif_format(const std::vector<AdifRecord>& records);

// Union onto archive. Duplicate keys keep the archive record.
// Does not apply the live logger's 10-minute same-call window.
AdifMergeStatus adif_merge_export(const std::string& archive,
                                  const std::string& incoming,
                                  std::string& out);

// Same union, streaming. `archive` may be nullptr (empty SD file). Writes a
// Mini-FT8 header then records; does not hold either file in RAM.
AdifMergeStatus adif_merge_stdio(std::FILE* archive, std::FILE* incoming, std::FILE* out);

// In-memory same-call window for the live logger. Not used by merge/export.
// Callers pass a normalized call (uppercase, <> stripped). Empty call is ignored.
// ---------------------------------------------------------------------------
// Logger record formatting
//
// One QSO as an ADIF record line, terminated by <eor> and a newline. Lives here
// rather than inline in main.cpp so the field lengths and the omit rules are
// host-testable -- a wrong <tag:N> length is invisible on the radio and only
// shows up when someone imports the log months later.
//
// Omit rules: a field whose value is unknown is left out entirely rather than
// written as a zero-length tag. main.cpp already did this for rst_sent and
// rst_rcvd (citing DXFT8's ADIF.c) but not for gridsquare, so a QSO where the
// DX never sent a grid produced "<gridsquare:0> ". Legal ADIF, but
// inconsistent with the same record's own rst handling and picky importers
// dislike it.
// ---------------------------------------------------------------------------

// -99 means "no report" and omits the field, matching the autoseq sentinel.
inline constexpr int kAdifNoReport = -99;

struct AdifLogFields {
    std::string call;              // required
    std::string gridsquare;        // omitted when empty
    std::string mode;              // required
    std::string qso_date;          // YYYYMMDD
    std::string time_on;           // HHMMSS
    std::string freq;              // MHz, e.g. "14.074"
    std::string station_callsign;
    std::string my_gridsquare;     // omitted when empty
    int         rst_sent = kAdifNoReport;
    int         rst_rcvd = kAdifNoReport;
    std::string comment;           // omitted when empty
};

// Returns the record line including the trailing "<eor>\n".
std::string adif_format_log_record(const AdifLogFields& f);

static constexpr std::int64_t kAdifLoggerDedupeWindowMs = 10 * 60 * 1000LL;
static constexpr std::size_t kAdifLoggerDedupeMaxEntries = 32;

struct AdifLoggerDedupeEntry {
    std::string call;
    std::int64_t logged_ms = 0;
};

struct AdifLoggerDedupe {
    std::vector<AdifLoggerDedupeEntry> recent;
};

bool adif_logger_dedupe_is_duplicate(AdifLoggerDedupe* io,
                                     const std::string& call_norm,
                                     std::int64_t now_ms);

void adif_logger_dedupe_remember(AdifLoggerDedupe* io,
                                 const std::string& call_norm,
                                 std::int64_t now_ms);
