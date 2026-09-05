#include "qso_browse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

std::string ascii_lower_copy(const std::string& s)
{
    std::string out = s;
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string field_after_tag(const std::string& s, const std::string& s_lower, const char* tag)
{
    const size_t p = s_lower.find(std::string("<") + tag);
    if (p == std::string::npos) {
        return "";
    }
    const size_t gt = s.find('>', p);
    if (gt == std::string::npos) {
        return "";
    }
    size_t end = s.size();
    const size_t end_space = s.find(' ', gt + 1);
    const size_t end_tag = s.find('<', gt + 1);
    if (end_space != std::string::npos && end_space < end) {
        end = end_space;
    }
    if (end_tag != std::string::npos && end_tag < end) {
        end = end_tag;
    }
    return s.substr(gt + 1, end - gt - 1);
}

bool parse_rst(const std::string& raw, int* out)
{
    if (raw.empty()) {
        return false;
    }
    char* end = nullptr;
    long v = std::strtol(raw.c_str(), &end, 10);
    if (end == raw.c_str() || !end || *end != '\0') {
        return false;
    }
    if (v < -99) {
        v = -99;
    }
    if (v > 99) {
        v = 99;
    }
    *out = static_cast<int>(v);
    return true;
}

std::string trim_head(const std::string& in, size_t max_len)
{
    if (in.size() <= max_len) {
        return in;
    }
    if (max_len == 0) {
        return "";
    }
    if (max_len == 1) {
        return ">";
    }
    return in.substr(0, max_len - 1) + ">";
}

std::string format_signed3(bool has_value, int value)
{
    if (!has_value) {
        return "-??";
    }
    char out[4];
    std::snprintf(out, sizeof(out), "%+03d", value);
    return out;
}

std::string format_sent4(bool has_value, int value)
{
    if (!has_value) {
        return "S-??";
    }
    char out[5];
    std::snprintf(out, sizeof(out), "S%+03d", value);
    return out;
}

const char* band_name_for_mhz(double mhz, const QsoBrowseBand* bands, int band_count)
{
    if (!bands) {
        return nullptr;
    }
    for (int i = 0; i < band_count; ++i) {
        const double bm = static_cast<double>(bands[i].freq_khz) * 0.001;
        if (std::fabs(bm - mhz) < 0.1) {
            return bands[i].name;
        }
    }
    return nullptr;
}

bool parse_record_line(const std::string& line,
                       const QsoBrowseBand* bands,
                       int band_count,
                       QsoLogEntry* out)
{
    const std::string s_lower = ascii_lower_copy(line);
    if (s_lower.find("<call:") == std::string::npos) {
        return false;
    }

    std::string call = field_after_tag(line, s_lower, "call:");
    std::string time_on = field_after_tag(line, s_lower, "time_on:");
    std::string freq = field_after_tag(line, s_lower, "freq:");
    const std::string rst_rcvd_raw = field_after_tag(line, s_lower, "rst_rcvd:");
    const std::string rst_sent_raw = field_after_tag(line, s_lower, "rst_sent:");
    std::string band = freq;
    if (!freq.empty()) {
        const double mhz = std::atof(freq.c_str());
        const char* mapped = band_name_for_mhz(mhz, bands, band_count);
        if (mapped) {
            band = mapped;
        }
    }
    if (time_on.size() >= 4) {
        time_on = time_on.substr(0, 4);
        time_on.insert(2, ":");
    }
    if (time_on.size() != 5) {
        time_on = "??:??";
    }
    if (call.empty()) {
        call = "?";
    }
    if (band.empty()) {
        band = freq.empty() ? "?" : freq;
    }

    out->time_on = time_on;
    out->band = band;
    out->call = call;
    out->has_rst_rcvd = parse_rst(rst_rcvd_raw, &out->rst_rcvd);
    out->has_rst_sent = parse_rst(rst_sent_raw, &out->rst_sent);
    return true;
}

}  // namespace

bool qso_browse_is_daily_log_name(const std::string& name)
{
    if (name.size() != 12) {
        return false;
    }
    for (int i = 0; i < 8; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[static_cast<size_t>(i)]))) {
            return false;
        }
    }
    if (name[8] != '.') {
        return false;
    }
    const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(name[9])));
    const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(name[10])));
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(name[11])));
    return (a == 'a' && b == 'd' && c == 'i') ||
           (a == 't' && b == 'x' && c == 't');
}

void qso_browse_select_daily_files(const std::vector<std::string>& all_names,
                                   std::vector<std::string>* daily_out)
{
    daily_out->clear();
    for (const std::string& name : all_names) {
        if (qso_browse_is_daily_log_name(name)) {
            daily_out->push_back(name);
        }
    }
    std::sort(daily_out->begin(), daily_out->end(), std::greater<std::string>());
}

void qso_browse_fill_file_lines(const std::vector<std::string>& files,
                                std::vector<std::string>* lines)
{
    lines->clear();
    if (files.empty()) {
        lines->push_back("No YYYYMMDD.adi");
        return;
    }
    for (const std::string& name : files) {
        lines->push_back(name);
    }
}

void qso_browse_pager_reset(QsoBrowsePager* io, int skip, int take)
{
    io->skip = skip;
    io->take = take;
    io->matched = 0;
    io->has_next = false;
    io->entries.clear();
}

bool qso_browse_pager_feed(QsoBrowsePager* io,
                           const std::string& line,
                           const QsoBrowseBand* bands,
                           int band_count)
{
    QsoLogEntry entry;
    if (!parse_record_line(line, bands, band_count, &entry)) {
        return true;
    }
    if (io->matched++ < io->skip) {
        return true;
    }
    if (static_cast<int>(io->entries.size()) >= io->take) {
        io->has_next = true;
        return false;
    }
    io->entries.push_back(entry);
    return true;
}

void qso_browse_format_entry_lines(const std::vector<QsoLogEntry>& entries,
                                   QsoBrowsePageView view,
                                   std::vector<std::string>* lines)
{
    lines->clear();
    for (const QsoLogEntry& e : entries) {
        std::string call_field = trim_head(e.call, 11);
        if (call_field.size() < 11) {
            call_field.append(11 - call_field.size(), ' ');
        }
        if (view == QsoBrowsePageView::Alternate) {
            const std::string rcvd = format_signed3(e.has_rst_rcvd, e.rst_rcvd);
            const std::string sent = format_sent4(e.has_rst_sent, e.rst_sent);
            lines->push_back(call_field + rcvd + " " + sent);
        } else {
            const std::string band_disp = trim_head(e.band, 6);
            lines->push_back(e.time_on + " " + band_disp + " " + call_field);
        }
    }
    if (lines->empty()) {
        lines->push_back("No QSOs");
    }
}
