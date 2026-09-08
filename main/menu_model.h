#pragma once

// ============================================================================
// menu_model.h
//
// The pure half of the MENU: how many rows there are, which page and key each
// row answers to, and what characters each row's inline editor accepts. No
// globals, no display, no autoseq -- so it is host-testable, which the rest of
// the menu is not.
//
// main.cpp owns the other half: the label and action function for each row.
// Its table is index-parallel to kMenuRows below and static_asserts that the
// counts agree.
//
// This exists because both defects found when the menu became a table were in
// this logic, not in the labels or the actions: a dead edit-index branch, and
// a character filter that tested `menu_edit_idx % 6` -- "position on page" --
// which matched the numeric Max Retry row by accident (17 % 6 == 5). Both are
// covered by host_test_menu_model now.
// ============================================================================

#include <cstddef>
#include <cstdint>

// Rows per page. Matches ui_draw_list()'s 6-line page.
inline constexpr int kMenuRowsPerPage = 6;

// What an inline editor on this row accepts.
enum class MenuEdit : uint8_t {
    None,      // row has no inline edit
    Callsign,  // any printable, forced upper case
    Numeric,   // digits only
};

// A row's stable identity and edit behaviour. Order is on-screen order.
struct MenuRow {
    const char* id;
    MenuEdit    edit;
};

extern const MenuRow kMenuRows[];
int menu_row_count(void);
int menu_page_count(void);

// Layout. Page and key are derived from the index, never stored twice.
int  menu_page_of(int idx);          // -1 if idx is out of range
char menu_key_of(int idx);           // 0 if idx is out of range
int  menu_index_for(int page, char key);   // -1 if no such row

MenuEdit    menu_edit_class(int idx);       // MenuEdit::None if out of range
const char* menu_row_id(int idx);           // "" if out of range

// Character-filter policy for an inline edit in progress.
// Returns true if `c` should be appended. `out_ch` receives the character to
// append (upper-cased for Callsign rows). A false return means the keystroke
// is rejected outright: no append, and the caller must not redraw.
bool menu_edit_accepts(MenuEdit kind, char c, size_t current_len, char* out_ch);

// ---------------------------------------------------------------------------
// Long edit: the full-screen editor MENU uses for the free text, the comment,
// and the ignore list. Each kind has its own character rules, which used to be
// two nested conditions inside the key handler.
//
// There was a fourth kind, LONG_ACTIVE, for the old ActiveBand text. Band
// config (O then 3) replaced it and nothing has set it since, so its commit
// branch was unreachable; it is not represented here.
// ---------------------------------------------------------------------------
enum class MenuLongEdit : uint8_t {
    None,
    FreeText,    // upper-cased, no length cap
    Comment,     // taken as typed, no length cap
    IgnoreList,  // upper-cased, capped at kMenuIgnoreMaxLen
};

inline constexpr size_t kMenuIgnoreMaxLen = 64;

// Character-filter policy for a long edit, same contract as
// menu_edit_accepts(): false means reject outright, and `out_ch` receives the
// character to append when true.
bool menu_long_accepts(MenuLongEdit kind, char c, size_t current_len, char* out_ch);

// Length cap for a kind, or 0 when uncapped.
size_t menu_long_max_len(MenuLongEdit kind);

// Longest inline-edit buffer a Numeric row will accept.
inline constexpr size_t kMenuNumericMaxLen = 10;
