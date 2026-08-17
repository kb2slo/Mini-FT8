#pragma once

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
