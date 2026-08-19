#include "adif.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

bool is_space(char c) {
    switch (c) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            return true;
        default:
            return false;
    }
}

void skip_ws(const std::string& s, size_t& pos) {
    while (pos < s.size() && is_space(s[pos])) {
        ++pos;
    }
}

std::string ascii_lower(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string ascii_upper(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string trim_copy(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && is_space(s[begin])) {
        ++begin;
    }
    while (end > begin && is_space(s[end - 1])) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string strip_bom(const std::string& s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        return s.substr(3);
    }
    return s;
}

std::string basename_of(const std::string& name_or_path) {
    const size_t slash = name_or_path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return name_or_path;
    }
    return name_or_path.substr(slash + 1);
}

std::string normalize_call(std::string s) {
    s = trim_copy(s);
    if (!s.empty() && s.front() == '<') {
        s.erase(s.begin());
    }
    if (!s.empty() && s.back() == '>') {
        s.pop_back();
    }
    return ascii_upper(s);
}

// Parse one ADIF tag at pos. Data tags consume `length` bytes after `>`.
bool parse_tag(const std::string& s, size_t pos,
               std::string& name, std::string& value,
               size_t& tag_start, size_t& after) {
    const size_t lt = s.find('<', pos);
    if (lt == std::string::npos) {
        return false;
    }
    const size_t gt = s.find('>', lt + 1);
    if (gt == std::string::npos) {
        return false;
    }

    const std::string inner = s.substr(lt + 1, gt - lt - 1);
    size_t i = 0;
    while (i < inner.size() && inner[i] != ':') {
        ++i;
    }
    name = ascii_lower(trim_copy(inner.substr(0, i)));
    if (name.empty()) {
        return false;
    }

    int length = -1;
    if (i < inner.size() && inner[i] == ':') {
        ++i;
        size_t len_end = i;
        while (len_end < inner.size() &&
               std::isdigit(static_cast<unsigned char>(inner[len_end]))) {
            ++len_end;
        }
        if (len_end == i) {
            return false;
        }
        length = 0;
        for (size_t n = i; n < len_end; ++n) {
            length = length * 10 + (inner[n] - '0');
        }
    }

    tag_start = lt;
    if (length < 0) {
        value.clear();
        after = gt + 1;
        return true;
    }
    if (gt + 1 + static_cast<size_t>(length) > s.size()) {
        return false;
    }
    value = s.substr(gt + 1, static_cast<size_t>(length));
    after = gt + 1 + static_cast<size_t>(length);
    return true;
}

bool find_tag_named(const std::string& s, const char* want, size_t from,
                    size_t& tag_start, size_t& after) {
    size_t pos = from;
    std::string name;
    std::string value;
    while (parse_tag(s, pos, name, value, tag_start, after)) {
        if (name == want) {
            return true;
        }
        pos = (after > pos) ? after : (tag_start + 1);
    }
    return false;
}

bool field_value(const std::string& rec, const char* field, std::string& value) {
    size_t pos = 0;
    std::string name;
    std::string v;
    size_t tag_start = 0;
    size_t after = 0;
    while (parse_tag(rec, pos, name, v, tag_start, after)) {
        if (name == field) {
            value = v;
            return true;
        }
        if (name == "eor") {
            return false;
        }
        pos = (after > pos) ? after : (tag_start + 1);
    }
    return false;
}

bool record_from_text(const std::string& raw, AdifRecord& rec) {
    const std::string text = trim_copy(raw);
    if (text.empty()) {
        return false;
    }

    std::string call;
    std::string date;
    std::string time_on;
    if (!field_value(text, "call", call) ||
        !field_value(text, "qso_date", date) ||
        !field_value(text, "time_on", time_on)) {
        return false;
    }
    call = normalize_call(call);
    date = trim_copy(date);
    time_on = trim_copy(time_on);
    if (call.empty() || date.empty() || time_on.empty()) {
        return false;
    }

    std::string station;
    if (field_value(text, "station_callsign", station)) {
        station = normalize_call(station);
    }

    rec.text = text;
    rec.key = station + "\x1f" + call + "\x1f" + date + "\x1f" + time_on;
    return true;
}

void append_unique(std::vector<AdifRecord>& out,
                   std::unordered_set<std::string>& keys,
                   const std::vector<AdifRecord>& incoming) {
    for (const AdifRecord& rec : incoming) {
        if (keys.insert(rec.key).second) {
            out.push_back(rec);
        }
    }
}

void logger_dedupe_prune(AdifLoggerDedupe* io, std::int64_t now_ms) {
    auto& recent = io->recent;
    recent.erase(
        std::remove_if(recent.begin(), recent.end(),
                       [&](const AdifLoggerDedupeEntry& e) {
                           return (now_ms - e.logged_ms) > kAdifLoggerDedupeWindowMs;
                       }),
        recent.end());
    if (recent.size() > kAdifLoggerDedupeMaxEntries) {
        recent.erase(
            recent.begin(),
            recent.begin() +
                static_cast<std::ptrdiff_t>(recent.size() - kAdifLoggerDedupeMaxEntries));
    }
}

}  // namespace

bool adif_is_adi_filename(const std::string& name_or_path) {
    const std::string name = basename_of(name_or_path);
    if (name.size() < 4) {
        return false;
    }
    const std::string ext = ascii_lower(name.substr(name.size() - 4));
    return ext == ".adi";
}

bool adif_parse(const std::string& content, std::vector<AdifRecord>& records) {
    records.clear();
    const std::string text = strip_bom(content);

    size_t pos = 0;
    skip_ws(text, pos);
    if (pos >= text.size()) {
        return true;
    }

    size_t eoh_start = 0;
    size_t eoh_after = 0;
    if (find_tag_named(text, "eoh", pos, eoh_start, eoh_after)) {
        pos = eoh_after;
    }

    while (true) {
        skip_ws(text, pos);
        if (pos >= text.size()) {
            return true;
        }

        size_t eor_start = 0;
        size_t eor_after = 0;
        if (!find_tag_named(text, "eor", pos, eor_start, eor_after)) {
            return false;
        }

        AdifRecord rec;
        if (!record_from_text(text.substr(pos, eor_after - pos), rec)) {
            return false;
        }
        records.push_back(rec);
        pos = eor_after;
    }
}

std::string adif_format(const std::vector<AdifRecord>& records) {
    std::string out = "ADIF EXPORT\n<eoh>\n";
    for (const AdifRecord& rec : records) {
        out += rec.text;
        if (out.empty() || out.back() != '\n') {
            out += '\n';
        }
    }
    return out;
}

AdifMergeStatus adif_merge_export(const std::string& archive,
                                  const std::string& incoming,
                                  std::string& out) {
    out.clear();
    std::vector<AdifRecord> archive_recs;
    std::vector<AdifRecord> incoming_recs;
    if (!adif_parse(archive, archive_recs)) {
        return AdifMergeStatus::PARSE_ARCHIVE;
    }
    if (!adif_parse(incoming, incoming_recs)) {
        return AdifMergeStatus::PARSE_INCOMING;
    }

    std::vector<AdifRecord> merged;
    merged.reserve(archive_recs.size() + incoming_recs.size());
    std::unordered_set<std::string> keys;
    keys.reserve(archive_recs.size() + incoming_recs.size());
    append_unique(merged, keys, archive_recs);
    append_unique(merged, keys, incoming_recs);
    out = adif_format(merged);
    return AdifMergeStatus::OK;
}

bool adif_logger_dedupe_is_duplicate(AdifLoggerDedupe* io,
                                     const std::string& call_norm,
                                     std::int64_t now_ms) {
    if (call_norm.empty()) {
        return false;
    }
    logger_dedupe_prune(io, now_ms);
    for (const AdifLoggerDedupeEntry& e : io->recent) {
        if (e.call == call_norm) {
            return true;
        }
    }
    return false;
}

void adif_logger_dedupe_remember(AdifLoggerDedupe* io,
                                 const std::string& call_norm,
                                 std::int64_t now_ms) {
    if (call_norm.empty()) {
        return;
    }
    logger_dedupe_prune(io, now_ms);
    for (AdifLoggerDedupeEntry& e : io->recent) {
        if (e.call == call_norm) {
            e.logged_ms = now_ms;
            return;
        }
    }
    io->recent.push_back(AdifLoggerDedupeEntry{call_norm, now_ms});
}
