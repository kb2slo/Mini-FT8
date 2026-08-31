#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/time.h>

// SIG Current Time (0x2A2B). UTC. No ESP-IDF. Host tests and firmware both link this.

static constexpr std::size_t kCtsCurrentTimeSize = 10;

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

bool cts_parse_current_time(const std::uint8_t* data, std::size_t len, CtsCurrentTime* out);
bool cts_current_time_to_timeval(const CtsCurrentTime& ct, struct timeval* tv);
bool cts_parse_current_time_to_timeval(const std::uint8_t* data, std::size_t len, struct timeval* tv);
