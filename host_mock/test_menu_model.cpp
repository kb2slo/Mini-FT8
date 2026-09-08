// Host test for the pure half of the MENU: layout arithmetic, row identity,
// and the inline-edit character filter.
//
// Two cases here are regressions for defects that shipped, and both are marked
// REGRESSION below. Neither was catchable before this model existed, because
// the logic lived inline in a 141-line if/else chain inside main.cpp.

#include "menu_model.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        g_fail++;
    }
}

static void check_eq_int(int got, int want, const char* what) {
    if (got != want) {
        std::printf("FAIL: %s (got %d, want %d)\n", what, got, want);
        g_fail++;
    }
}

static void check_eq_str(const char* got, const char* want, const char* what) {
    if (std::strcmp(got, want) != 0) {
        std::printf("FAIL: %s (got \"%s\", want \"%s\")\n", what, got, want);
        g_fail++;
    }
}

// --- layout ---------------------------------------------------------------

static void test_shape(void) {
    check_eq_int(menu_row_count(), 18, "row count");
    check_eq_int(menu_page_count(), 3, "page count");
    check_eq_int(kMenuRowsPerPage, 6, "rows per page");
}

static void test_page_and_key(void) {
    // Row 0 is page 0 key '1'; row 17 is page 2 key '6'.
    check_eq_int(menu_page_of(0), 0, "row 0 page");
    check(menu_key_of(0) == '1', "row 0 key");
    check_eq_int(menu_page_of(5), 0, "row 5 page");
    check(menu_key_of(5) == '6', "row 5 key");
    check_eq_int(menu_page_of(6), 1, "row 6 page");
    check(menu_key_of(6) == '1', "row 6 key");
    check_eq_int(menu_page_of(17), 2, "row 17 page");
    check(menu_key_of(17) == '6', "row 17 key");
}

static void test_roundtrip(void) {
    // Every row must map to a (page, key) that maps back to the same row.
    // This is the invariant the old code restated by hand in three places.
    for (int i = 0; i < menu_row_count(); ++i) {
        const int page = menu_page_of(i);
        const char key = menu_key_of(i);
        const int back = menu_index_for(page, key);
        if (back != i) {
            std::printf("FAIL: roundtrip row %d -> (p%d,'%c') -> %d\n", i, page, key, back);
            g_fail++;
        }
    }
}

static void test_out_of_range(void) {
    check_eq_int(menu_page_of(-1), -1, "page_of(-1)");
    check_eq_int(menu_page_of(18), -1, "page_of(18)");
    check(menu_key_of(-1) == 0, "key_of(-1)");
    check(menu_key_of(18) == 0, "key_of(18)");
    check_eq_int(menu_index_for(-1, '1'), -1, "index_for(page -1)");
    check_eq_int(menu_index_for(3, '1'), -1, "index_for(page 3) -- no 4th page");
    check_eq_int(menu_index_for(0, '0'), -1, "index_for key '0'");
    check_eq_int(menu_index_for(0, '7'), -1, "index_for key '7'");
    check_eq_int(menu_index_for(0, 'a'), -1, "index_for key 'a'");
    check(menu_edit_class(-1) == MenuEdit::None, "edit_class(-1)");
    check(menu_edit_class(99) == MenuEdit::None, "edit_class(99)");
    check_eq_str(menu_row_id(-1), "", "row_id(-1)");
}

// --- identity -------------------------------------------------------------

static void test_ids(void) {
    // Pins the on-screen order. Reordering a row without updating main.cpp's
    // parallel label/action table would move an action under a new label; this
    // makes the order itself an assertion.
    static const char* kExpected[] = {
        "cq_type", "send_ft", "freetext", "call", "grid", "sleep_batt",
        "offset_src", "offset_hz", "radio", "ignore_list", "comment", "protocol",
        "rxtx_log", "skip_tx1", "band_config", "gnss_lora", "copy_to_sd", "max_retry",
    };
    check_eq_int((int)(sizeof(kExpected) / sizeof(kExpected[0])), menu_row_count(),
                 "expected-id list length matches row count");
    for (int i = 0; i < menu_row_count(); ++i) {
        check_eq_str(menu_row_id(i), kExpected[i], "row id");
    }
    // No duplicate ids -- an id is how a row is named in the test plan.
    for (int i = 0; i < menu_row_count(); ++i) {
        for (int j = i + 1; j < menu_row_count(); ++j) {
            if (std::strcmp(menu_row_id(i), menu_row_id(j)) == 0) {
                std::printf("FAIL: duplicate row id \"%s\" at %d and %d\n", menu_row_id(i), i, j);
                g_fail++;
            }
        }
    }
}

// --- edit classes ---------------------------------------------------------

static void test_edit_classes(void) {
    // Exactly four rows have an inline editor.
    check(menu_edit_class(3) == MenuEdit::Callsign, "call is Callsign");
    check(menu_edit_class(4) == MenuEdit::Callsign, "grid is Callsign");
    check(menu_edit_class(7) == MenuEdit::Numeric, "offset_hz is Numeric");
    check(menu_edit_class(17) == MenuEdit::Numeric, "max_retry is Numeric");

    int editable = 0;
    for (int i = 0; i < menu_row_count(); ++i) {
        if (menu_edit_class(i) != MenuEdit::None) editable++;
    }
    check_eq_int(editable, 4, "exactly four editable rows");

    // REGRESSION: the old filter tested `menu_edit_idx % 6 == 3 || 4 || 5`,
    // meaning "position on page". 17 % 6 == 5, so Max Retry -- a digits-only
    // field -- was silently treated as an upper-casing text field. Harmless
    // only because toupper('7') == '7'; it would have broken as soon as a row
    // was added or reordered. Max Retry must be Numeric, never Callsign.
    check(menu_edit_class(17) != MenuEdit::Callsign,
          "REGRESSION: max_retry must not inherit Callsign via idx % 6 == 5");

    // REGRESSION: `menu_edit_idx == 10` was a live branch that could never
    // run -- comment editing goes through the long-edit path, not an inline
    // editor. Row 10 must have no inline edit at all.
    check(menu_edit_class(10) == MenuEdit::None,
          "REGRESSION: comment row has no inline editor");
    check_eq_str(menu_row_id(10), "comment", "row 10 is the comment row");

    // The other dead index the old code compared against.
    check(menu_edit_class(15) == MenuEdit::None, "row 15 has no inline editor");
}

// --- character filter -----------------------------------------------------

static void test_filter_callsign(void) {
    char out = 0;
    check(menu_edit_accepts(MenuEdit::Callsign, 'k', 0, &out) && out == 'K',
          "callsign lower-cases to upper");
    check(menu_edit_accepts(MenuEdit::Callsign, 'B', 0, &out) && out == 'B',
          "callsign passes upper through");
    check(menu_edit_accepts(MenuEdit::Callsign, '2', 0, &out) && out == '2',
          "callsign accepts digits");
    check(menu_edit_accepts(MenuEdit::Callsign, '/', 0, &out) && out == '/',
          "callsign accepts portable slash");
    check(!menu_edit_accepts(MenuEdit::Callsign, '\n', 0, &out),
          "callsign rejects newline");
    check(!menu_edit_accepts(MenuEdit::Callsign, (char)0x7f, 0, &out),
          "callsign rejects DEL");
}

static void test_filter_numeric(void) {
    char out = 0;
    check(menu_edit_accepts(MenuEdit::Numeric, '0', 0, &out) && out == '0',
          "numeric accepts 0");
    check(menu_edit_accepts(MenuEdit::Numeric, '9', 3, &out) && out == '9',
          "numeric accepts 9");
    check(!menu_edit_accepts(MenuEdit::Numeric, 'a', 0, &out), "numeric rejects letter");
    check(!menu_edit_accepts(MenuEdit::Numeric, '-', 0, &out), "numeric rejects minus");
    check(!menu_edit_accepts(MenuEdit::Numeric, ' ', 0, &out), "numeric rejects space");

    // Length cap: accepts up to the cap, rejects at it.
    check(menu_edit_accepts(MenuEdit::Numeric, '1', kMenuNumericMaxLen - 1, &out),
          "numeric accepts at cap - 1");
    check(!menu_edit_accepts(MenuEdit::Numeric, '1', kMenuNumericMaxLen, &out),
          "numeric rejects at cap");
    check(!menu_edit_accepts(MenuEdit::Numeric, '1', kMenuNumericMaxLen + 5, &out),
          "numeric rejects past cap");
}

static void test_filter_none(void) {
    char out = 0;
    check(!menu_edit_accepts(MenuEdit::None, 'a', 0, &out),
          "None class never accepts");
    check(!menu_edit_accepts(MenuEdit::None, '1', 0, &out),
          "None class rejects digits too");
}

static void test_filter_null_out(void) {
    // A caller that only wants the accept/reject decision must not crash.
    check(menu_edit_accepts(MenuEdit::Numeric, '5', 0, nullptr), "null out_ch ok");
    check(!menu_edit_accepts(MenuEdit::Numeric, 'z', 0, nullptr), "null out_ch reject ok");
}

int main(void) {
    test_shape();
    test_page_and_key();
    test_roundtrip();
    test_out_of_range();
    test_ids();
    test_edit_classes();
    test_filter_callsign();
    test_filter_numeric();
    test_filter_none();
    test_filter_null_out();

    if (g_fail) {
        std::printf("FAILED: %d check(s)\n", g_fail);
        return 1;
    }
    std::printf("PASS: menu model\n");
    return 0;
}
