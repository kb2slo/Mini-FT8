#include "station.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace {

std::string trim_copy(const std::string& s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

void ascii_upper_inplace(std::string& s)
{
    for (char& ch : s) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
}

std::string trim_upper_copy(const std::string& s)
{
    std::string out = trim_copy(s);
    ascii_upper_inplace(out);
    return out;
}

std::string normalize_grid_maidenhead(const std::string& src)
{
    size_t b = 0;
    size_t e = src.size();
    while (b < e && std::isspace(static_cast<unsigned char>(src[b]))) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(src[e - 1]))) {
        --e;
    }

    const size_t n = e - b;
    if (n != 4 && n != 6 && n != 8) {
        return "";
    }

    std::string out = src.substr(b, n);
    auto is_digit_char = [](char ch) { return ch >= '0' && ch <= '9'; };
    auto to_upper = [](char ch) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    };
    auto to_lower = [](char ch) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    };

    char c0 = to_upper(out[0]);
    char c1 = to_upper(out[1]);
    if (c0 < 'A' || c0 > 'R' || c1 < 'A' || c1 > 'R') {
        return "";
    }
    if (!is_digit_char(out[2]) || !is_digit_char(out[3])) {
        return "";
    }
    out[0] = c0;
    out[1] = c1;

    if (n >= 6) {
        char c4 = to_upper(out[4]);
        char c5 = to_upper(out[5]);
        if (c4 < 'A' || c4 > 'X' || c5 < 'A' || c5 > 'X') {
            return "";
        }
        out[4] = to_lower(c4);
        out[5] = to_lower(c5);
    }

    if (n == 8) {
        if (!is_digit_char(out[6]) || !is_digit_char(out[7])) {
            return "";
        }
    }

    return out;
}

int normalize_gps_baud(int value)
{
    return (value == 9600 || value == 115200) ? value : 115200;
}

int parse_radio_value(const char* raw)
{
    if (!raw) {
        return 2;  // QMX
    }

    char* end = nullptr;
    long as_int = strtol(raw, &end, 10);
    if (end != raw) {
        switch (static_cast<int>(as_int)) {
            case 5:
                return 5;
            case 2:
            default:
                return 2;
        }
    }

    std::string token;
    for (const char* p = raw; *p; ++p) {
        unsigned char ch = static_cast<unsigned char>(*p);
        if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') {
            continue;
        }
        token.push_back(static_cast<char>(std::toupper(ch)));
    }

    if (token == "QDX") {
        return 5;
    }
    return 2;
}

void format_freq(float f, char* fbuf, size_t n)
{
    if (f == static_cast<int>(f)) {
        snprintf(fbuf, n, "%d", static_cast<int>(f));
    } else {
        snprintf(fbuf, n, "%.1f", f);
    }
}

enum class StationKey {
    Offset,
    BandSel,
    CqType,
    OffsetSrc,
    Radio,
    GpsBaud,
    GnssLora,
    GpsSource,
    CqFt,
    FreeText,
    Call,
    Grid,
    Comment1,
    IgnorePrefixes,
    RxtxLog,
    SkipTx1,
    ActiveBand,
    ActiveBands,
    AutoseqMaxRetry,
    ProtocolMode,
    Beacon,
    Unknown,
};

StationKey key_from_name(const std::string& name)
{
    if (name == "offset") return StationKey::Offset;
    if (name == "band_sel") return StationKey::BandSel;
    if (name == "cq_type") return StationKey::CqType;
    if (name == "offset_src") return StationKey::OffsetSrc;
    if (name == "radio") return StationKey::Radio;
    if (name == "gps_baud") return StationKey::GpsBaud;
    if (name == "gnss_lora") return StationKey::GnssLora;
    if (name == "gps_source") return StationKey::GpsSource;
    if (name == "cq_ft") return StationKey::CqFt;
    if (name == "free_text") return StationKey::FreeText;
    if (name == "call") return StationKey::Call;
    if (name == "grid") return StationKey::Grid;
    if (name == "comment1") return StationKey::Comment1;
    if (name == "ignore_prefixes") return StationKey::IgnorePrefixes;
    if (name == "rxtx_log") return StationKey::RxtxLog;
    if (name == "skiptx1") return StationKey::SkipTx1;
    if (name == "active_band") return StationKey::ActiveBand;
    if (name == "active_bands") return StationKey::ActiveBands;
    if (name == "autoseq_max_retry") return StationKey::AutoseqMaxRetry;
    if (name == "protocol_mode") return StationKey::ProtocolMode;
    if (name == "beacon") return StationKey::Beacon;
    return StationKey::Unknown;
}

void apply_key(StationSettings* io, StationKey key, const std::string& value)
{
    int val = 0;
    switch (key) {
        case StationKey::Offset:
            if (sscanf(value.c_str(), "%d", &val) == 1) {
                io->offset_hz = val;
            }
            break;
        case StationKey::BandSel:
            if (sscanf(value.c_str(), "%d", &val) == 1) {
                if (val >= 0 && val < kStationBandCount) {
                    io->band_sel = val;
                }
            }
            break;
        case StationKey::CqType:
            if (sscanf(value.c_str(), "%d", &val) == 1) {
                if (val >= 0 && val <= 5) {
                    io->cq_type = val;
                }
            }
            break;
        case StationKey::OffsetSrc:
            if (sscanf(value.c_str(), "%d", &val) == 1) {
                if (val >= 0 && val <= 2) {
                    io->offset_src = val;
                }
            }
            break;
        case StationKey::Radio:
            io->radio = parse_radio_value(value.c_str());
            break;
        case StationKey::GpsBaud:
            if (sscanf(value.c_str(), "%d", &val) == 1) {
                io->gps_baud = normalize_gps_baud(val);
            }
            break;
        case StationKey::GnssLora:
            if (sscanf(value.c_str(), "%d", &val) == 1) {
                io->gnss_lora = (val != 0);
            }
            break;
        case StationKey::GpsSource:
            if (sscanf(value.c_str(), "%d", &val) == 1 && val == 2) {
                io->gnss_lora = true;
            }
            break;
        case StationKey::CqFt:
            io->cq_freetext = trim_upper_copy(value);
            break;
        case StationKey::FreeText:
            io->free_text = trim_upper_copy(value);
            break;
        case StationKey::Call:
            io->call = trim_upper_copy(value);
            break;
        case StationKey::Grid: {
            const std::string norm = normalize_grid_maidenhead(value);
            if (!norm.empty()) {
                io->grid = norm;
            }
            break;
        }
        case StationKey::Comment1:
            io->comment1 = trim_copy(value);
            break;
        case StationKey::IgnorePrefixes:
            io->ignore_prefixes = trim_upper_copy(value);
            break;
        case StationKey::RxtxLog:
            if (sscanf(value.c_str(), "%d", &val) == 1) {
                io->rxtx_log = (val != 0);
            }
            break;
        case StationKey::SkipTx1:
            if (sscanf(value.c_str(), "%d", &val) == 1) {
                io->skip_tx1 = (val != 0);
            }
            break;
        case StationKey::ActiveBand:
            if (sscanf(value.c_str(), "%d", &val) == 1) {
                io->active_bands = std::to_string(val);
            }
            break;
        case StationKey::ActiveBands:
            io->active_bands = trim_upper_copy(value);
            break;
        case StationKey::AutoseqMaxRetry:
            if (sscanf(value.c_str(), "%d", &val) == 1 && val >= 0) {
                io->autoseq_max_retry = val;
            }
            break;
        case StationKey::ProtocolMode: {
            std::string mode = trim_upper_copy(value);
            io->protocol_ft4 = (mode == "FT4");
            break;
        }
        case StationKey::Beacon:
        case StationKey::Unknown:
            break;
    }
}

void apply_line(StationSettings* io, const std::string& raw)
{
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line.empty()) {
        return;
    }

    int idx = -1;
    float fval = 0.0f;
    if (sscanf(line.c_str(), "ft4_band%d=%f", &idx, &fval) == 2) {
        if (idx >= 0 && idx < kStationBandCount) {
            io->ft4_band_freq[idx] = fval;
            io->ft4_band_freq_set[idx] = true;
        }
        return;
    }
    if (sscanf(line.c_str(), "band%d=%f", &idx, &fval) == 2) {
        if (idx >= 0 && idx < kStationBandCount) {
            io->band_freq[idx] = fval;
            io->band_freq_set[idx] = true;
        }
        return;
    }

    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
        return;
    }
    apply_key(io, key_from_name(line.substr(0, eq)), line.substr(eq + 1));
}

}  // namespace

void station_settings_init(StationSettings* out)
{
    if (!out) {
        return;
    }
    *out = StationSettings{};
    out->gps_baud = 115200;
    out->radio = 2;
}

void station_parse(const std::string& text, StationSettings* io)
{
    if (!io) {
        return;
    }
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            apply_line(io, text.substr(start));
            break;
        }
        apply_line(io, text.substr(start, end - start));
        start = end + 1;
    }
}

std::string station_serialize(const StationSettings& in)
{
    std::ostringstream out;
    const char* band_prefix = in.serialize_ft4_band_keys ? "ft4_band" : "band";
    const float* freqs = in.serialize_ft4_band_keys ? in.ft4_band_freq : in.band_freq;
    const bool* set = in.serialize_ft4_band_keys ? in.ft4_band_freq_set : in.band_freq_set;
    for (int i = 0; i < kStationBandCount; ++i) {
        if (!set[i]) {
            continue;
        }
        char fbuf[16];
        format_freq(freqs[i], fbuf, sizeof(fbuf));
        out << band_prefix << i << "=" << fbuf << "\n";
    }
    out << "offset=" << in.offset_hz << "\n";
    out << "band_sel=" << in.band_sel << "\n";
    out << "cq_type=" << in.cq_type << "\n";
    out << "cq_ft=" << in.cq_freetext << "\n";
    out << "skiptx1=" << (in.skip_tx1 ? 1 : 0) << "\n";
    out << "free_text=" << in.free_text << "\n";
    out << "call=" << in.call << "\n";
    out << "grid=" << in.grid << "\n";
    out << "offset_src=" << in.offset_src << "\n";
    out << "radio=" << in.radio << "\n";
    out << "gps_baud=" << normalize_gps_baud(in.gps_baud) << "\n";
    out << "gnss_lora=" << (in.gnss_lora ? 1 : 0) << "\n";
    out << "comment1=" << in.comment1 << "\n";
    out << "ignore_prefixes=" << in.ignore_prefixes << "\n";
    out << "rxtx_log=" << (in.rxtx_log ? 1 : 0) << "\n";
    out << "active_bands=" << in.active_bands << "\n";
    out << "autoseq_max_retry=" << in.autoseq_max_retry << "\n";
    if (in.protocol_ft4) {
        out << "protocol_mode=FT4\n";
    }
    return out.str();
}
