#pragma once

#include <cstdint>
#include <string>

// Station.txt parse/serialize. No ESP-IDF. Host tests and firmware both link this.
// Radio integers match main/station_types.h RadioType values.

static constexpr int kStationBandCount = 12;

struct StationSettings {
    float band_freq[kStationBandCount];
    bool band_freq_set[kStationBandCount];
    float ft4_band_freq[kStationBandCount];
    bool ft4_band_freq_set[kStationBandCount];

    int offset_hz;
    int band_sel;
    std::string date;
    std::string time;
    int cq_type;
    std::string cq_freetext;
    bool skip_tx1;
    std::string free_text;
    std::string call;
    std::string grid;
    int offset_src;
    int radio;
    int gps_baud;
    bool gnss_lora;
    std::string comment1;
    std::string ignore_prefixes;
    bool rxtx_log;
    std::string active_bands;
    std::int64_t rtc_sleep_epoch;
    int rtc_comp;
    int autoseq_max_retry;
    bool protocol_ft4;
    bool serialize_ft4_band_keys;
};

void station_settings_init(StationSettings* out);

// Overlay key=value lines onto *io. Unknown keys ignored. beacon= ignored.
// date=/time= never sscanf into the source line.
void station_parse(const std::string& text, StationSettings* io);

// Writes the same keys save_station_data() does. No beacon= line.
std::string station_serialize(const StationSettings& in);
