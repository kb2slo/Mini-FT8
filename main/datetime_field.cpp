#include "datetime_field.h"

#include <cstring>

namespace {

bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

// Two digits at `off`, or -1 if either is not a digit.
int two_digits(const char* s, size_t off)
{
    if (!is_digit(s[off]) || !is_digit(s[off + 1])) {
        return -1;
    }
    return (s[off] - '0') * 10 + (s[off + 1] - '0');
}

int four_digits(const char* s, size_t off)
{
    for (size_t i = 0; i < 4; ++i) {
        if (!is_digit(s[off + i])) {
            return -1;
        }
    }
    return (s[off] - '0') * 1000 + (s[off + 1] - '0') * 100 +
           (s[off + 2] - '0') * 10 + (s[off + 3] - '0');
}

}  // namespace

bool datetime_field_is_separator(char c)
{
    switch (c) {
        case '-':
        case ':':
            return true;
        default:
            return false;
    }
}

int datetime_field_cursor_first(const char* buf, size_t len)
{
    if (!buf) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!datetime_field_is_separator(buf[i])) {
            return (int)i;
        }
    }
    return -1;
}

int datetime_field_cursor_left(const char* buf, size_t len, int pos)
{
    if (!buf || pos <= 0) {
        return pos;
    }
    if (pos > (int)len) {
        pos = (int)len;
    }
    int p = pos - 1;
    while (p >= 0 && datetime_field_is_separator(buf[p])) {
        p--;
    }
    return (p >= 0) ? p : pos;
}

int datetime_field_cursor_right(const char* buf, size_t len, int pos)
{
    if (!buf || pos < 0) {
        return pos;
    }
    int p = pos + 1;
    while (p < (int)len && datetime_field_is_separator(buf[p])) {
        p++;
    }
    return (p < (int)len) ? p : pos;
}

int datetime_field_set_digit(char* buf, size_t len, int pos, char digit)
{
    if (!buf || pos < 0 || pos >= (int)len) {
        return pos;
    }
    if (!is_digit(digit)) {
        return pos;
    }
    buf[pos] = digit;
    return datetime_field_cursor_right(buf, len, pos);
}

bool datetime_field_is_leap_year(int year)
{
    if ((year % 4) != 0) {
        return false;
    }
    if ((year % 100) != 0) {
        return true;
    }
    return (year % 400) == 0;
}

int datetime_field_days_in_month(int year, int month)
{
    switch (month) {
        case 1: case 3: case 5: case 7: case 8: case 10: case 12:
            return 31;
        case 4: case 6: case 9: case 11:
            return 30;
        case 2:
            return datetime_field_is_leap_year(year) ? 29 : 28;
        default:
            return 0;
    }
}

bool datetime_field_date_valid(const char* date)
{
    if (!date || std::strlen(date) != kDateFieldLen) {
        return false;
    }
    if (date[4] != '-' || date[7] != '-') {
        return false;
    }
    const int year  = four_digits(date, 0);
    const int month = two_digits(date, 5);
    const int day   = two_digits(date, 8);
    if (year < 2000 || year > 2099) {
        return false;
    }
    if (month < 1 || month > 12) {
        return false;
    }
    const int max_day = datetime_field_days_in_month(year, month);
    return day >= 1 && day <= max_day;
}

bool datetime_field_time_valid(const char* time)
{
    if (!time || std::strlen(time) != kTimeFieldLen) {
        return false;
    }
    if (time[2] != ':' || time[5] != ':') {
        return false;
    }
    const int hour = two_digits(time, 0);
    const int min  = two_digits(time, 3);
    const int sec  = two_digits(time, 6);
    if (hour < 0 || hour > 23) {
        return false;
    }
    if (min < 0 || min > 59) {
        return false;
    }
    return sec >= 0 && sec <= 59;
}
