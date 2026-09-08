#pragma once

// ============================================================================
// screen_model.h
//
// Which screen a key goes to, and what pressing that key again does. The pure
// half of the mode-switch block in main.cpp -- no display, no globals -- so it
// is host-testable (host_test_screen_model).
//
// The rule this captures is not uniform, which is why it was worth extracting:
//
//   R           always enters RX (no toggle)
//   T B Q D S G H   toggle: pressing the key while on that screen returns to RX
//   M N O       all three enter MENU, at pages 0 / 1 / 2. Pressing the key
//               while already on its own page returns to RX; on a different
//               MENU page it moves to that page instead
//   P           PERF doubles as the log viewer: RX -> stats -> log -> RX
//
// Written out as twelve else-if branches those four shapes are easy to get
// subtly wrong and impossible to test. Here they are one function.
// ============================================================================

#include <cstdint>

enum class UIMode : uint8_t {
    RX, TX, BAND, MENU, DEBUG, STATUS, QSO, GPS, PERF, BT
};

// Human-readable screen name (used by the UART screen mirror and logs).
const char* screen_name(UIMode mode);

// What a screen key should do.
enum class ScreenAction : uint8_t {
    None,           // not a screen key; caller handles it
    Enter,          // enter `screen`
    EnterMenuPage,  // enter MENU, then show page `page`
    SetMenuPage,    // already in MENU: show page `page`, no mode change
    ShowPerfLog,    // already in PERF stats: switch to the log page
    LeaveToRx,      // return to RX
};

struct ScreenNav {
    ScreenAction action;
    UIMode       screen;  // meaningful when action == Enter
    int          page;    // meaningful for EnterMenuPage / SetMenuPage
};

// `key` is the raw keystroke (upper or lower case both work).
// `current` is the screen showing now. `menu_page` is the current MENU page
// and `perf_page` the current PERF sub-page; both are ignored unless the key
// is M/N/O or P respectively.
ScreenNav screen_nav_for_key(char key, UIMode current, int menu_page, int perf_page);

// True if `key` is one of the screen keys at all.
bool screen_key_is_nav(char key);

// The MENU page a key targets, or -1 if the key is not M/N/O. Exposed so the
// caller and the tests agree on the M/N/O -> page mapping in one place.
int screen_menu_page_for_key(char key);
