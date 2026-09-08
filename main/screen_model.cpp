#include "screen_model.h"

#include <cctype>

namespace {

ScreenNav enter(UIMode screen)
{
    return { ScreenAction::Enter, screen, 0 };
}

ScreenNav leave_to_rx(void)
{
    return { ScreenAction::LeaveToRx, UIMode::RX, 0 };
}

// A plain toggle key: enter its screen, or leave to RX if already there.
ScreenNav toggle(UIMode screen, UIMode current)
{
    return (current == screen) ? leave_to_rx() : enter(screen);
}

// M / N / O: enter MENU at `page`; pressing the key while already on that page
// leaves; on a different MENU page it moves there instead.
ScreenNav menu_nav(int page, UIMode current, int menu_page)
{
    if (current != UIMode::MENU) {
        return { ScreenAction::EnterMenuPage, UIMode::MENU, page };
    }
    if (menu_page == page) {
        return leave_to_rx();
    }
    return { ScreenAction::SetMenuPage, UIMode::MENU, page };
}

}  // namespace

const char* screen_name(UIMode mode)
{
    // Exhaustive enum switch, no default: -Wswitch catches a new UIMode.
    switch (mode) {
        case UIMode::RX:     return "RX";
        case UIMode::TX:     return "TX";
        case UIMode::BAND:   return "BAND";
        case UIMode::MENU:   return "MENU";
        case UIMode::DEBUG:  return "DEBUG";
        case UIMode::STATUS: return "STATUS";
        case UIMode::QSO:    return "QSO";
        case UIMode::GPS:    return "GPS";
        case UIMode::PERF:   return "PERF";
        case UIMode::BT:     return "BT";
    }
    return "?";
}

int screen_menu_page_for_key(char key)
{
    switch (std::tolower((unsigned char)key)) {
        case 'm':  return 0;
        case 'n':  return 1;
        case 'o':  return 2;
        default:   return -1;
    }
}

bool screen_key_is_nav(char key)
{
    switch (std::tolower((unsigned char)key)) {
        case 'r':
        case 't':
        case 'b':
        case 'm':
        case 'n':
        case 'o':
        case 'q':
        case 'd':
        case 's':
        case 'g':
        case 'h':
        case 'p':
            return true;
        default:
            return false;
    }
}

ScreenNav screen_nav_for_key(char key, UIMode current, int menu_page, int perf_page)
{
    switch (std::tolower((unsigned char)key)) {
        // R always enters RX, even from RX. It is the "get me home" key.
        case 'r':
            return enter(UIMode::RX);

        // PERF doubles as the log viewer: stats -> log -> RX.
        case 'p':
            if (current != UIMode::PERF) return enter(UIMode::PERF);
            if (perf_page == 0)          return { ScreenAction::ShowPerfLog, UIMode::PERF, 1 };
            return leave_to_rx();

        case 'm': return menu_nav(0, current, menu_page);
        case 'n': return menu_nav(1, current, menu_page);
        case 'o': return menu_nav(2, current, menu_page);

        case 't': return toggle(UIMode::TX,     current);
        case 'b': return toggle(UIMode::BAND,   current);
        case 'q': return toggle(UIMode::QSO,    current);
        case 'd': return toggle(UIMode::DEBUG,  current);   // Delete Files
        case 's': return toggle(UIMode::STATUS, current);
        case 'g': return toggle(UIMode::GPS,    current);
        case 'h': return toggle(UIMode::BT,     current);

        default:
            return { ScreenAction::None, current, 0 };
    }
}
