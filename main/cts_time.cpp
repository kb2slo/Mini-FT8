#include <cstdint>
#include <ctime>

#include "cts_time.h"

static bool is_leap_year(int year)
{
    if (year % 400 == 0) {
        return true;
    }
    if (year % 100 == 0) {
        return false;
    }
    return (year % 4) == 0;
}

static int days_in_month(int year, int month)
{
    switch (month) {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            return 31;
        case 4:
        case 6:
        case 9:
        case 11:
            return 30;
        case 2:
            return is_leap_year(year) ? 29 : 28;
        default:
            return 0;
    }
}

static bool utc_to_unix(const CtsCurrentTime& ct, time_t* out)
{
    if (out == nullptr) {
        return false;
    }
    if (ct.year < 1970 || ct.year > 2100) {
        return false;
    }
    if (ct.hour < 0 || ct.hour > 23 || ct.minute < 0 || ct.minute > 59 || ct.second < 0 ||
        ct.second > 59) {
        return false;
    }
    const int dim = days_in_month(ct.year, ct.month);
    if (dim == 0 || ct.day < 1 || ct.day > dim) {
        return false;
    }
    if (ct.fractions256 < 0 || ct.fractions256 > 255) {
        return false;
    }

    int days = 0;
    for (int y = 1970; y < ct.year; ++y) {
        days += is_leap_year(y) ? 366 : 365;
    }
    for (int m = 1; m < ct.month; ++m) {
        days += days_in_month(ct.year, m);
    }
    days += ct.day - 1;

    *out = static_cast<time_t>(days) * 86400 + ct.hour * 3600 + ct.minute * 60 + ct.second;
    return true;
}

bool cts_parse_current_time(const std::uint8_t* data, std::size_t len, CtsCurrentTime* out)
{
    if (data == nullptr || out == nullptr || len != kCtsCurrentTimeSize) {
        return false;
    }

    CtsCurrentTime ct;
    ct.year = static_cast<int>(data[0] | (static_cast<unsigned>(data[1]) << 8));
    ct.month = data[2];
    ct.day = data[3];
    ct.hour = data[4];
    ct.minute = data[5];
    ct.second = data[6];
    ct.day_of_week = data[7];
    ct.fractions256 = data[8];
    ct.adjust_reason = data[9];

    time_t dummy = 0;
    if (!utc_to_unix(ct, &dummy)) {
        return false;
    }

    *out = ct;
    return true;
}

bool cts_current_time_to_timeval(const CtsCurrentTime& ct, struct timeval* tv)
{
    if (tv == nullptr) {
        return false;
    }
    time_t sec = 0;
    if (!utc_to_unix(ct, &sec)) {
        return false;
    }
    tv->tv_sec = sec;
    tv->tv_usec = static_cast<suseconds_t>((static_cast<std::uint32_t>(ct.fractions256) * 1000000u) / 256u);
    return true;
}

bool cts_parse_current_time_to_timeval(const std::uint8_t* data, std::size_t len, struct timeval* tv)
{
    CtsCurrentTime ct;
    if (!cts_parse_current_time(data, len, &ct)) {
        return false;
    }
    return cts_current_time_to_timeval(ct, tv);
}

bool cts_parse_local_time_information(const std::uint8_t* data, std::size_t len, CtsLocalTimeInfo* out)
{
    if (data == nullptr || out == nullptr || len != kCtsLocalTimeInfoSize) {
        return false;
    }
    if (data[0] == 0x80u) {
        return false;
    }
    const int timezone_15min = static_cast<int>(static_cast<std::int8_t>(data[0]));
    if (timezone_15min < -48 || timezone_15min > 56) {
        return false;
    }

    int dst_offset_seconds = 0;
    switch (data[1]) {
        case 0x00:
            dst_offset_seconds = 0;
            break;
        case 0x02:
            dst_offset_seconds = 30 * 60;
            break;
        case 0x04:
            dst_offset_seconds = 60 * 60;
            break;
        case 0x08:
            dst_offset_seconds = 2 * 60 * 60;
            break;
        default:
            return false;
    }

    out->timezone_15min = timezone_15min;
    out->dst_offset_seconds = dst_offset_seconds;
    return true;
}

bool cts_local_to_utc_timeval(const CtsCurrentTime& local, const CtsLocalTimeInfo& lti, struct timeval* tv)
{
    if (!cts_current_time_to_timeval(local, tv)) {
        return false;
    }
    // Local = UTC + (timezone × 15 min) + DST. Subtract those to seed UTC.
    const time_t offset =
        static_cast<time_t>(lti.timezone_15min) * 15 * 60 + static_cast<time_t>(lti.dst_offset_seconds);
    tv->tv_sec -= offset;
    return true;
}

bool cts_parse_phone_time_to_utc_timeval(const std::uint8_t* current_time, std::size_t current_time_len,
                                         const std::uint8_t* local_time_info, std::size_t local_time_info_len,
                                         struct timeval* tv)
{
    CtsCurrentTime local;
    CtsLocalTimeInfo lti;
    if (!cts_parse_current_time(current_time, current_time_len, &local)) {
        return false;
    }
    if (!cts_parse_local_time_information(local_time_info, local_time_info_len, &lti)) {
        return false;
    }
    return cts_local_to_utc_timeval(local, lti, tv);
}
