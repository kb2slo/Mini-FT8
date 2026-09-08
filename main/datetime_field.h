#pragma once

// ============================================================================
// datetime_field.h
//
// The STATUS screen's in-place date and time editor: a fixed-layout field with
// separators ("2026-09-07", "23:59:59") edited by moving a cursor over the
// digit positions and overwriting them. Pure -- no display, no RTC, no globals
// -- so it is host-testable (host_test_datetime_field).
//
// It also carries the range validation the firmware was missing. The old path
// checked a manually entered date with sscanf("%d-%d-%d") followed by mktime(),
// which does NOT reject out-of-range fields: mktime normalises them. Measured
// on this toolchain, "2026-02-30" became 2026-03-02, "2026-13-45" became
// 2027-02-14, and "2026-00-00" became 2025-11-30 -- each silently accepted and
// written to the RTC while the UI claimed to validate. datetime_field_valid()
// is the strict check that mktime is not.
// ============================================================================

#include <cstddef>

// Layout of the two editable fields.
inline constexpr size_t kDateFieldLen = 10;  // YYYY-MM-DD
inline constexpr size_t kTimeFieldLen = 8;   // HH:MM:SS

// True for a layout separator ('-' or ':'), which the cursor skips over.
bool datetime_field_is_separator(char c);

// First editable position in `buf`, or -1 if it has none.
int datetime_field_cursor_first(const char* buf, size_t len);

// Next / previous editable position. Returns `pos` unchanged when there is no
// further position in that direction, matching the screen's behaviour of
// stopping at the ends rather than wrapping.
int datetime_field_cursor_left(const char* buf, size_t len, int pos);
int datetime_field_cursor_right(const char* buf, size_t len, int pos);

// Overwrite the digit at `pos` and return the cursor position to land on
// (the next editable position, or `pos` when already at the end). `digit` must
// be '0'..'9'; anything else leaves the buffer untouched and returns `pos`.
int datetime_field_set_digit(char* buf, size_t len, int pos, char digit);

// Strict validation. Both must match their exact layout and be in range:
// year 2000-2099, month 1-12, day 1..days-in-month (leap-aware),
// hour 0-23, minute 0-59, second 0-59.
bool datetime_field_date_valid(const char* date);
bool datetime_field_time_valid(const char* time);

// Days in a month, leap-aware. Returns 0 for an out-of-range month.
int datetime_field_days_in_month(int year, int month);
bool datetime_field_is_leap_year(int year);
