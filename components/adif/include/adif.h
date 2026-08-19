#pragma once

#include <cstdint>
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
};

bool adif_is_adi_filename(const std::string& name_or_path);

// Empty content is a valid 0-record file. Non-ADIF leftover text fails.
bool adif_parse(const std::string& content, std::vector<AdifRecord>& records);

std::string adif_format(const std::vector<AdifRecord>& records);

// Union onto archive. Duplicate keys keep the archive record.
// Does not apply the live logger's 10-minute same-call window.
AdifMergeStatus adif_merge_export(const std::string& archive,
                                  const std::string& incoming,
                                  std::string& out);

// In-memory same-call window for the live logger. Not used by merge/export.
// Callers pass a normalized call (uppercase, <> stripped). Empty call is ignored.
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
