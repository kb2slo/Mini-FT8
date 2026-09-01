#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/time.h>

// SIG Current Time (0x2A2B) is local civil time on Apple / nRF Connect.
// UTC needs Local Time Information (0x2A0F). No ESP-IDF. Host tests and firmware both link this.

static constexpr std::size_t kCtsCurrentTimeSize = 10;
static constexpr std::size_t kCtsLocalTimeInfoSize = 2;

struct CtsCurrentTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int day_of_week = 0;  // 0 unknown, 1 Monday .. 7 Sunday; unused for epoch
    int fractions256 = 0;
    int adjust_reason = 0;
};

struct CtsLocalTimeInfo {
    int timezone_15min = 0;  // -48..+56; 15-minute units from UTC
    int dst_offset_seconds = 0;
};

bool cts_parse_current_time(const std::uint8_t* data, std::size_t len, CtsCurrentTime* out);
bool cts_current_time_to_timeval(const CtsCurrentTime& ct, struct timeval* tv);
bool cts_parse_current_time_to_timeval(const std::uint8_t* data, std::size_t len, struct timeval* tv);

bool cts_parse_local_time_information(const std::uint8_t* data, std::size_t len, CtsLocalTimeInfo* out);
bool cts_local_to_utc_timeval(const CtsCurrentTime& local, const CtsLocalTimeInfo& lti, struct timeval* tv);
bool cts_parse_phone_time_to_utc_timeval(const std::uint8_t* current_time, std::size_t current_time_len,
                                         const std::uint8_t* local_time_info, std::size_t local_time_info_len,
                                         struct timeval* tv);
