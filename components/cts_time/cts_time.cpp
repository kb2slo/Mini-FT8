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
