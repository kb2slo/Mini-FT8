// Host test for the STATUS date/time in-place editor: cursor movement over a
// fixed layout with separators, digit overwrite, and range validation.
//
// The validation half is a REGRESSION suite. The firmware validated a manually
// entered date with sscanf("%d-%d-%d") plus mktime(), which normalises
// out-of-range fields rather than rejecting them -- so every realistic bad
// date was silently accepted and written to the RTC. The cases marked
// REGRESSION below are the exact inputs measured doing that.

#include "datetime_field.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        g_fail++;
    }
}

static void check_eq(int got, int want, const char* what)
{
    if (got != want) {
        std::printf("FAIL: %s (got %d, want %d)\n", what, got, want);
        g_fail++;
    }
}

static void reject(const char* date, const char* why)
{
    if (datetime_field_date_valid(date)) {
        std::printf("FAIL: %s -- \"%s\" was accepted\n", why, date);
        g_fail++;
    }
}

static void accept(const char* date, const char* why)
{
    if (!datetime_field_date_valid(date)) {
        std::printf("FAIL: %s -- \"%s\" was rejected\n", why, date);
        g_fail++;
    }
}

// --- separators and cursor ------------------------------------------------

static void test_separators(void)
{
    check(datetime_field_is_separator('-'), "dash is a separator");
    check(datetime_field_is_separator(':'), "colon is a separator");
    check(!datetime_field_is_separator('0'), "digit is not a separator");
    check(!datetime_field_is_separator('/'), "slash is not a separator");
}

static void test_cursor_first(void)
{
    check_eq(datetime_field_cursor_first("2026-09-07", kDateFieldLen), 0, "date first pos");
    check_eq(datetime_field_cursor_first("23:59:59", kTimeFieldLen), 0, "time first pos");
    check_eq(datetime_field_cursor_first("--------", 8), -1, "all separators -> none");
    check_eq(datetime_field_cursor_first(nullptr, 8), -1, "null buffer");
}

static void test_cursor_right_skips_separators(void)
{
    const char* d = "2026-09-07";
    const size_t n = kDateFieldLen;
    // 0 1 2 3 [-] 5 6 [-] 8 9  ->  positions 3 and 5 must be adjacent
    check_eq(datetime_field_cursor_right(d, n, 0), 1, "0 -> 1");
    check_eq(datetime_field_cursor_right(d, n, 2), 3, "2 -> 3");
    check_eq(datetime_field_cursor_right(d, n, 3), 5, "3 -> 5, skipping the dash");
    check_eq(datetime_field_cursor_right(d, n, 6), 8, "6 -> 8, skipping the dash");
    check_eq(datetime_field_cursor_right(d, n, 9), 9, "last position does not move");

    const char* t = "23:59:59";
    check_eq(datetime_field_cursor_right(t, kTimeFieldLen, 1), 3, "time 1 -> 3, skipping colon");
    check_eq(datetime_field_cursor_right(t, kTimeFieldLen, 4), 6, "time 4 -> 6, skipping colon");
    check_eq(datetime_field_cursor_right(t, kTimeFieldLen, 7), 7, "time last does not move");
}

static void test_cursor_left_skips_separators(void)
{
    const char* d = "2026-09-07";
    const size_t n = kDateFieldLen;
    check_eq(datetime_field_cursor_left(d, n, 9), 8, "9 -> 8");
    check_eq(datetime_field_cursor_left(d, n, 8), 6, "8 -> 6, skipping the dash");
    check_eq(datetime_field_cursor_left(d, n, 5), 3, "5 -> 3, skipping the dash");
    check_eq(datetime_field_cursor_left(d, n, 0), 0, "first position does not move");

    const char* t = "23:59:59";
    check_eq(datetime_field_cursor_left(t, kTimeFieldLen, 3), 1, "time 3 -> 1, skipping colon");
    check_eq(datetime_field_cursor_left(t, kTimeFieldLen, 0), 0, "time first does not move");
}

static void test_cursor_roundtrip(void)
{
    // Walking right to the end then left back must return to the start,
    // visiting the same positions. This is the invariant the inline version
    // restated as two near-identical while loops.
    const char* d = "2026-09-07";
    const size_t n = kDateFieldLen;
    int pos = datetime_field_cursor_first(d, n);
    int seen[16];
    int count = 0;
    for (;;) {
        seen[count++] = pos;
        const int next = datetime_field_cursor_right(d, n, pos);
        if (next == pos) break;
        pos = next;
    }
    check_eq(count, 8, "date has 8 editable positions");
    for (int i = count - 1; i > 0; --i) {
        pos = datetime_field_cursor_left(d, n, pos);
        check_eq(pos, seen[i - 1], "left walk retraces the right walk");
    }
}

static void test_set_digit(void)
{
    char buf[] = "2026-09-07";
    const size_t n = kDateFieldLen;

    int pos = datetime_field_set_digit(buf, n, 3, '9');
    check(std::strcmp(buf, "2029-09-07") == 0, "digit written at 3");
    check_eq(pos, 5, "cursor advanced past the dash");

    pos = datetime_field_set_digit(buf, n, 9, '1');
    check(std::strcmp(buf, "2029-09-01") == 0, "digit written at the last position");
    check_eq(pos, 9, "cursor stays at the last position");

    // Non-digits and out-of-range positions leave the buffer alone.
    char before[16];
    std::strcpy(before, buf);
    check_eq(datetime_field_set_digit(buf, n, 0, 'x'), 0, "non-digit returns pos");
    check(std::strcmp(buf, before) == 0, "non-digit does not modify the buffer");
    check_eq(datetime_field_set_digit(buf, n, -1, '5'), -1, "negative pos returns pos");
    check_eq(datetime_field_set_digit(buf, n, 99, '5'), 99, "past-end pos returns pos");
    check(std::strcmp(buf, before) == 0, "bad positions do not modify the buffer");
}

// --- validation -----------------------------------------------------------

static void test_valid_dates(void)
{
    accept("2026-09-07", "ordinary date");
    accept("2000-01-01", "lower year bound");
    accept("2099-12-31", "upper year bound");
    accept("2024-02-29", "leap day in a leap year");
    accept("2000-02-29", "leap day in 2000, divisible by 400");
}

static void test_invalid_dates(void)
{
    // REGRESSION: measured behaviour of the old sscanf + mktime path. Each of
    // these was silently accepted and rolled over into a different date.
    reject("2026-13-45", "REGRESSION: mktime rolled this to 2027-02-14");
    reject("2026-02-30", "REGRESSION: mktime rolled this to 2026-03-02");
    reject("2026-00-00", "REGRESSION: mktime rolled this to 2025-11-30");

    reject("2025-02-29", "Feb 29 in a non-leap year");
    reject("2100-02-29", "2100 is not a leap year, divisible by 100 not 400");
    reject("2026-04-31", "April has 30 days");
    reject("2026-01-32", "day past the end of January");
    reject("1999-01-01", "year below the range");
    reject("2100-01-01", "year above the range");
    reject("2026-9-07",  "month not zero-padded");
    reject("2026/09/07", "wrong separators");
    reject("2026-09-7",  "short buffer");
    reject("2026-09-070", "long buffer");
    reject("20a6-09-07", "non-digit in the year");
    reject("", "empty");
    reject(nullptr, "null");
}

static void test_time_validation(void)
{
    check(datetime_field_time_valid("00:00:00"), "midnight");
    check(datetime_field_time_valid("23:59:59"), "last second of the day");
    check(datetime_field_time_valid("12:34:56"), "ordinary time");

    check(!datetime_field_time_valid("24:00:00"), "hour 24 rejected");
    check(!datetime_field_time_valid("23:60:00"), "minute 60 rejected");
    check(!datetime_field_time_valid("23:59:60"), "second 60 rejected");
    check(!datetime_field_time_valid("1:23:45"),  "hour not zero-padded");
    check(!datetime_field_time_valid("12-34-56"), "wrong separators");
    check(!datetime_field_time_valid("12:34"),    "short buffer");
    check(!datetime_field_time_valid(""),         "empty");
    check(!datetime_field_time_valid(nullptr),    "null");
}

static void test_days_in_month(void)
{
    check_eq(datetime_field_days_in_month(2026, 1), 31, "January");
    check_eq(datetime_field_days_in_month(2026, 2), 28, "February, non-leap");
    check_eq(datetime_field_days_in_month(2024, 2), 29, "February, leap");
    check_eq(datetime_field_days_in_month(2026, 4), 30, "April");
    check_eq(datetime_field_days_in_month(2026, 12), 31, "December");
    check_eq(datetime_field_days_in_month(2026, 0), 0, "month 0 is out of range");
    check_eq(datetime_field_days_in_month(2026, 13), 0, "month 13 is out of range");

    check(datetime_field_is_leap_year(2024), "2024 is a leap year");
    check(!datetime_field_is_leap_year(2025), "2025 is not");
    check(!datetime_field_is_leap_year(2100), "2100 is not, divisible by 100");
    check(datetime_field_is_leap_year(2000), "2000 is, divisible by 400");
}

static void test_every_day_of_a_year(void)
{
    // Exhaustive sweep: every day of 2026 and 2024 must validate, and the day
    // after each month's end must not.
    const int years[] = { 2024, 2026 };
    for (int y : years) {
        for (int m = 1; m <= 12; ++m) {
            const int last = datetime_field_days_in_month(y, m);
            for (int d = 1; d <= last; ++d) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
                if (!datetime_field_date_valid(buf)) {
                    std::printf("FAIL: real date \"%s\" rejected\n", buf);
                    g_fail++;
                }
            }
            char over[16];
            std::snprintf(over, sizeof(over), "%04d-%02d-%02d", y, m, last + 1);
            if (datetime_field_date_valid(over)) {
                std::printf("FAIL: overflow date \"%s\" accepted\n", over);
                g_fail++;
            }
        }
    }
}

int main(void)
{
    test_separators();
    test_cursor_first();
    test_cursor_right_skips_separators();
    test_cursor_left_skips_separators();
    test_cursor_roundtrip();
    test_set_digit();
    test_valid_dates();
    test_invalid_dates();
    test_time_validation();
    test_days_in_month();
    test_every_day_of_a_year();

    if (g_fail) {
        std::printf("FAILED: %d check(s)\n", g_fail);
        return 1;
    }
    std::printf("PASS: datetime field\n");
    return 0;
}
