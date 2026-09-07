#include "adif.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
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

constexpr size_t kAdifStreamMaxCarry = 96u * 1024u;
const char kAdifExportHeader[] = "ADIF EXPORT\n<eoh>\n";

void append_record_line(std::string& out, const AdifRecord& rec) {
    out += rec.text;
    if (out.empty() || out.back() != '\n') {
        out += '\n';
    }
}

bool write_record_file(std::FILE* file, const AdifRecord& rec) {
    if (!file) {
        return false;
    }
    if (!rec.text.empty() &&
        std::fwrite(rec.text.data(), 1, rec.text.size(), file) != rec.text.size()) {
        return false;
    }
    if (rec.text.empty() || rec.text.back() != '\n') {
        if (std::fputc('\n', file) == EOF) {
            return false;
        }
    }
    return true;
}

bool collect_record(const AdifRecord& rec, void* ctx) {
    static_cast<std::vector<AdifRecord>*>(ctx)->push_back(rec);
    return true;
}

struct StringMergeCtx {
    std::unordered_set<std::string>* keys = nullptr;
    std::string* out = nullptr;
    bool archive_pass = false;
};

bool string_merge_record(const AdifRecord& rec, void* ctx) {
    auto* st = static_cast<StringMergeCtx*>(ctx);
    if (st->archive_pass) {
        st->keys->insert(rec.key);
        append_record_line(*st->out, rec);
        return true;
    }
    if (!st->keys->insert(rec.key).second) {
        return true;
    }
    append_record_line(*st->out, rec);
    return true;
}

struct FileMergeCtx {
    std::unordered_set<std::string>* keys = nullptr;
    std::FILE* out = nullptr;
    bool archive_pass = false;
};

bool file_merge_record(const AdifRecord& rec, void* ctx) {
    auto* st = static_cast<FileMergeCtx*>(ctx);
    if (st->archive_pass) {
        st->keys->insert(rec.key);
        return write_record_file(st->out, rec);
    }
    if (!st->keys->insert(rec.key).second) {
        return true;
    }
    return write_record_file(st->out, rec);
}

bool stream_file_records(std::FILE* file, AdifRecordFn fn, void* ctx) {
    AdifStream st;
    if (!file) {
        return adif_stream_feed(&st, nullptr, 0, true, fn, ctx);
    }
    char chunk[512];
    while (true) {
        const size_t n = std::fread(chunk, 1, sizeof(chunk), file);
        if (n > 0 && !adif_stream_feed(&st, chunk, n, false, fn, ctx)) {
            return false;
        }
        if (n < sizeof(chunk)) {
            if (std::ferror(file)) {
                return false;
            }
            return adif_stream_feed(&st, nullptr, 0, true, fn, ctx);
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
    AdifStream st;
    return adif_stream_feed(&st, content.data(), content.size(), true, collect_record, &records);
}

bool adif_stream_feed(AdifStream* io, const char* data, size_t len, bool eof,
                      AdifRecordFn on_record, void* ctx) {
    if (io == nullptr || io->failed) {
        return false;
    }
    if (data != nullptr && len > 0) {
        io->buf.append(data, len);
    }

    if (!io->bom_done) {
        if (io->buf.size() >= 3 &&
            static_cast<unsigned char>(io->buf[0]) == 0xEF &&
            static_cast<unsigned char>(io->buf[1]) == 0xBB &&
            static_cast<unsigned char>(io->buf[2]) == 0xBF) {
            io->buf.erase(0, 3);
            io->bom_done = true;
        } else if (io->buf.size() >= 3 || eof) {
            io->bom_done = true;
        } else {
            return true;
        }
    }

    size_t pos = 0;
    skip_ws(io->buf, pos);

    if (!io->header_done) {
        size_t eoh_start = 0;
        size_t eoh_after = 0;
        if (find_tag_named(io->buf, "eoh", pos, eoh_start, eoh_after)) {
            pos = eoh_after;
            io->header_done = true;
        } else {
            size_t eor_start = 0;
            size_t eor_after = 0;
            if (find_tag_named(io->buf, "eor", pos, eor_start, eor_after)) {
                io->header_done = true;
            } else if (eof) {
                skip_ws(io->buf, pos);
                if (pos < io->buf.size()) {
                    io->failed = true;
                    return false;
                }
                io->buf.clear();
                return true;
            } else if (io->buf.size() > kAdifStreamMaxCarry) {
                io->failed = true;
                return false;
            } else {
                return true;
            }
        }
    }

    while (io->header_done) {
        skip_ws(io->buf, pos);
        if (pos >= io->buf.size()) {
            if (eof) {
                io->buf.clear();
                return true;
            }
            io->buf.erase(0, pos);
            return true;
        }

        size_t eor_start = 0;
        size_t eor_after = 0;
        if (!find_tag_named(io->buf, "eor", pos, eor_start, eor_after)) {
            if (eof) {
                io->failed = true;
                return false;
            }
            io->buf.erase(0, pos);
            if (io->buf.size() > kAdifStreamMaxCarry) {
                io->failed = true;
                return false;
            }
            return true;
        }

        AdifRecord rec;
        if (!record_from_text(io->buf.substr(pos, eor_after - pos), rec)) {
            io->failed = true;
            return false;
        }
        if (on_record != nullptr && !on_record(rec, ctx)) {
            io->failed = true;
            return false;
        }
        pos = eor_after;
    }

    io->buf.erase(0, pos);
    return true;
}

std::string adif_format(const std::vector<AdifRecord>& records) {
    std::string out = kAdifExportHeader;
    for (const AdifRecord& rec : records) {
        append_record_line(out, rec);
    }
    return out;
}

AdifMergeStatus adif_merge_export(const std::string& archive,
                                  const std::string& incoming,
                                  std::string& out) {
    out.clear();
    out = kAdifExportHeader;
    std::unordered_set<std::string> keys;
    StringMergeCtx ctx;
    ctx.keys = &keys;
    ctx.out = &out;

    AdifStream archive_st;
    ctx.archive_pass = true;
    if (!adif_stream_feed(&archive_st, archive.data(), archive.size(), true,
                          string_merge_record, &ctx)) {
        out.clear();
        return AdifMergeStatus::PARSE_ARCHIVE;
    }

    AdifStream incoming_st;
    ctx.archive_pass = false;
    if (!adif_stream_feed(&incoming_st, incoming.data(), incoming.size(), true,
                          string_merge_record, &ctx)) {
        out.clear();
        return AdifMergeStatus::PARSE_INCOMING;
    }
    return AdifMergeStatus::OK;
}

AdifMergeStatus adif_merge_stdio(std::FILE* archive, std::FILE* incoming, std::FILE* out) {
    if (out == nullptr || incoming == nullptr) {
        return AdifMergeStatus::WRITE_FAILED;
    }
    const size_t header_len = std::strlen(kAdifExportHeader);
    if (std::fwrite(kAdifExportHeader, 1, header_len, out) != header_len) {
        return AdifMergeStatus::WRITE_FAILED;
    }

    std::unordered_set<std::string> keys;
    FileMergeCtx ctx;
    ctx.keys = &keys;
    ctx.out = out;

    ctx.archive_pass = true;
    if (!stream_file_records(archive, file_merge_record, &ctx)) {
        return std::ferror(out) ? AdifMergeStatus::WRITE_FAILED
                                : AdifMergeStatus::PARSE_ARCHIVE;
    }
    ctx.archive_pass = false;
    if (!stream_file_records(incoming, file_merge_record, &ctx)) {
        return std::ferror(out) ? AdifMergeStatus::WRITE_FAILED
                                : AdifMergeStatus::PARSE_INCOMING;
    }
    if (std::ferror(out)) {
        return AdifMergeStatus::WRITE_FAILED;
    }
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
