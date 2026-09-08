// Host test for the screen-navigation rules: which key goes to which screen,
// and what pressing that key again does.
//
// The rule is deliberately not uniform -- R never toggles, seven keys do,
// M/N/O share one screen across three pages, and P cycles through a sub-page.
// As twelve else-if branches in main.cpp those four shapes were untestable and
// easy to get subtly wrong.

#include "screen_model.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        g_fail++;
    }
}

static void check_nav(ScreenNav got, ScreenAction action, const char* what)
{
    if (got.action != action) {
        std::printf("FAIL: %s (action %d, want %d)\n", what, (int)got.action, (int)action);
        g_fail++;
    }
}

static ScreenNav nav(char key, UIMode cur, int menu_page = 0, int perf_page = 0)
{
    return screen_nav_for_key(key, cur, menu_page, perf_page);
}

// --- names ----------------------------------------------------------------

static void test_names(void)
{
    check(std::strcmp(screen_name(UIMode::RX), "RX") == 0, "RX name");
    check(std::strcmp(screen_name(UIMode::MENU), "MENU") == 0, "MENU name");
    check(std::strcmp(screen_name(UIMode::BT), "BT") == 0, "BT name");
    // DEBUG is the Delete Files screen; the name follows the enum, not the UI.
    check(std::strcmp(screen_name(UIMode::DEBUG), "DEBUG") == 0, "DEBUG name");
    // Every screen must have a name that is not the fallback.
    const UIMode all[] = { UIMode::RX, UIMode::TX, UIMode::BAND, UIMode::MENU,
                           UIMode::DEBUG, UIMode::STATUS, UIMode::QSO, UIMode::GPS,
                           UIMode::PERF, UIMode::BT };
    for (UIMode m : all) {
        check(std::strcmp(screen_name(m), "?") != 0, "screen has a real name");
    }
}

// --- key recognition ------------------------------------------------------

static void test_key_recognition(void)
{
    const char* keys = "rtbmnoqdsghp";
    for (const char* k = keys; *k; ++k) {
        check(screen_key_is_nav(*k), "lower-case nav key recognised");
        check(screen_key_is_nav((char)(*k - 32)), "upper-case nav key recognised");
    }
    // Digits and menu paging keys are NOT screen keys -- they belong to the
    // screen currently showing.
    check(!screen_key_is_nav('1'), "'1' is not a nav key");
    check(!screen_key_is_nav('6'), "'6' is not a nav key");
    check(!screen_key_is_nav(';'), "';' is not a nav key");
    check(!screen_key_is_nav('.'), "'.' is not a nav key");
    check(!screen_key_is_nav('`'), "backtick is not a nav key");
    check(!screen_key_is_nav('\n'), "newline is not a nav key");
    // C was USB Drive; B23 removed it and the key must stay inert.
    check(!screen_key_is_nav('c'), "'c' is inert -- USB Drive was removed (B23)");
    check(!screen_key_is_nav('C'), "'C' is inert -- USB Drive was removed (B23)");
}

// --- R does not toggle ----------------------------------------------------

static void test_r_never_toggles(void)
{
    // Every other screen key returns to RX when pressed on its own screen.
    // R is the exception: it always enters RX, including from RX.
    ScreenNav n = nav('r', UIMode::RX);
    check_nav(n, ScreenAction::Enter, "R from RX enters RX");
    check(n.screen == UIMode::RX, "R targets RX");
    check_nav(nav('r', UIMode::MENU), ScreenAction::Enter, "R from MENU enters RX");
    check_nav(nav('R', UIMode::STATUS), ScreenAction::Enter, "upper R works");
}

// --- the seven plain toggles ---------------------------------------------

static void test_toggles(void)
{
    struct Case { char key; UIMode screen; const char* name; };
    static const Case kCases[] = {
        { 't', UIMode::TX,     "TX"     },
        { 'b', UIMode::BAND,   "BAND"   },
        { 'q', UIMode::QSO,    "QSO"    },
        { 'd', UIMode::DEBUG,  "DEBUG"  },
        { 's', UIMode::STATUS, "STATUS" },
        { 'g', UIMode::GPS,    "GPS"    },
        { 'h', UIMode::BT,     "BT"     },
    };
    for (const Case& c : kCases) {
        ScreenNav in = nav(c.key, UIMode::RX);
        check_nav(in, ScreenAction::Enter, "toggle key enters from RX");
        check(in.screen == c.screen, "toggle key targets its own screen");

        ScreenNav out = nav(c.key, c.screen);
        check_nav(out, ScreenAction::LeaveToRx, "toggle key leaves from its own screen");

        // From a third screen it switches directly, it does not go via RX.
        ScreenNav cross = nav(c.key, UIMode::GPS == c.screen ? UIMode::TX : UIMode::GPS);
        check_nav(cross, ScreenAction::Enter, "toggle key switches from another screen");
        check(cross.screen == c.screen, "cross-switch targets the right screen");

        check(nav((char)(c.key - 32), UIMode::RX).screen == c.screen, "upper case works");
    }
}

// --- M / N / O share MENU across three pages ------------------------------

static void test_menu_keys(void)
{
    check(screen_menu_page_for_key('m') == 0, "M is page 0");
    check(screen_menu_page_for_key('n') == 1, "N is page 1");
    check(screen_menu_page_for_key('o') == 2, "O is page 2");
    check(screen_menu_page_for_key('M') == 0, "upper M is page 0");
    check(screen_menu_page_for_key('r') == -1, "R has no menu page");
    check(screen_menu_page_for_key('1') == -1, "'1' has no menu page");

    // From outside MENU each key enters MENU at its own page.
    for (int page = 0; page < 3; ++page) {
        const char key = (char)("mno"[page]);
        ScreenNav n = nav(key, UIMode::RX);
        check_nav(n, ScreenAction::EnterMenuPage, "menu key enters MENU");
        check(n.screen == UIMode::MENU, "menu key targets MENU");
        check(n.page == page, "menu key enters at its own page");
    }

    // On its own page, the key leaves.
    check_nav(nav('m', UIMode::MENU, 0), ScreenAction::LeaveToRx, "M on page 0 leaves");
    check_nav(nav('n', UIMode::MENU, 1), ScreenAction::LeaveToRx, "N on page 1 leaves");
    check_nav(nav('o', UIMode::MENU, 2), ScreenAction::LeaveToRx, "O on page 2 leaves");

    // On a different MENU page, it moves to its page without leaving MENU.
    ScreenNav m_from_2 = nav('m', UIMode::MENU, 2);
    check_nav(m_from_2, ScreenAction::SetMenuPage, "M from page 2 moves page");
    check(m_from_2.page == 0, "M from page 2 goes to page 0");

    ScreenNav o_from_0 = nav('o', UIMode::MENU, 0);
    check_nav(o_from_0, ScreenAction::SetMenuPage, "O from page 0 moves page");
    check(o_from_0.page == 2, "O from page 0 goes to page 2");

    check_nav(nav('n', UIMode::MENU, 0), ScreenAction::SetMenuPage, "N from page 0 moves page");
    check(nav('n', UIMode::MENU, 2).page == 1, "N from page 2 goes to page 1");
}

// --- P cycles stats -> log -> RX -----------------------------------------

static void test_perf_cycle(void)
{
    ScreenNav in = nav('p', UIMode::RX);
    check_nav(in, ScreenAction::Enter, "P from RX enters PERF");
    check(in.screen == UIMode::PERF, "P targets PERF");

    check_nav(nav('p', UIMode::PERF, 0, 0), ScreenAction::ShowPerfLog,
              "P on PERF stats shows the log");
    check_nav(nav('p', UIMode::PERF, 0, 1), ScreenAction::LeaveToRx,
              "P on PERF log leaves to RX");

    // menu_page must not affect the PERF decision, and perf_page must not
    // affect the MENU decision -- the two sub-pages are independent.
    check_nav(nav('p', UIMode::PERF, 2, 0), ScreenAction::ShowPerfLog,
              "menu_page does not leak into the P rule");
    check_nav(nav('m', UIMode::MENU, 0, 1), ScreenAction::LeaveToRx,
              "perf_page does not leak into the M rule");
}

// --- non-nav keys ---------------------------------------------------------

static void test_non_nav_keys(void)
{
    const char* others = "123456;.`\n\r acCzZ";
    for (const char* k = others; *k; ++k) {
        if (screen_key_is_nav(*k)) continue;   // 'a' etc. genuinely are not nav
        check_nav(nav(*k, UIMode::RX), ScreenAction::None, "non-nav key yields None");
        check_nav(nav(*k, UIMode::MENU, 1), ScreenAction::None, "non-nav key None in MENU");
    }
    // Consistency: is_nav and nav_for_key must agree for every printable char.
    for (int ch = 32; ch < 127; ++ch) {
        const bool is_nav = screen_key_is_nav((char)ch);
        const bool acts = nav((char)ch, UIMode::RX).action != ScreenAction::None;
        if (is_nav != acts) {
            std::printf("FAIL: '%c' is_nav=%d but acts=%d\n", ch, (int)is_nav, (int)acts);
            g_fail++;
        }
    }
}

int main(void)
{
    test_names();
    test_key_recognition();
    test_r_never_toggles();
    test_toggles();
    test_menu_keys();
    test_perf_cycle();
    test_non_nav_keys();

    if (g_fail) {
        std::printf("FAILED: %d check(s)\n", g_fail);
        return 1;
    }
    std::printf("PASS: screen model\n");
    return 0;
}
