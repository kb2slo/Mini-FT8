#pragma once
#include <vector>
#include <string>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Screen layout constants — shared between ui.cpp and callers that need to
// position content relative to the waterfall / countdown areas.
// ---------------------------------------------------------------------------
#define SCREEN_W    240
#define SCREEN_H    135
#define WATERFALL_H  18    ///< Pixel height of the waterfall strip
#define COUNTDOWN_H   3    ///< Pixel height of the TX countdown bar
#define RX_LINES      6    ///< Visible text rows below the countdown bar
/// Y-coordinate where the text area begins (below waterfall + countdown).
#define UI_START_Y  (WATERFALL_H + COUNTDOWN_H)   // 21 px

// A lightweight RX line format you can fill from your decoder
struct UiRxLine {
    std::string text;  // already formatted for display
    int snr = 0;
    int offset_hz = 0; // audio-bin offset in Hz relative to passband center
    int slot_id = 0;   // 0 = even slot (0/30s), 1 = odd slot (15/45s)
    std::string field1; // parsed token 1 (call/CQ marker)
    std::string field2; // parsed token 2
    std::string field3; // parsed token 3 (grid/report/etc)
    bool is_cq = false;
    bool is_to_me = false;
};

// Plain-C RX entry used for zero-heap decode/display pipeline.
// Fixed-size char arrays avoid std::string heap allocations.
#define RX_MAX_DECODES  32
#define RX_TEXT_MAX     64
#define RX_FIELD_MAX    20

struct RxDecodeEntry {
    char text[RX_TEXT_MAX];
    char field1[RX_FIELD_MAX];
    char field2[RX_FIELD_MAX];
    char field3[RX_FIELD_MAX];
    int  snr;
    int  offset_hz;
    int  slot_id;
    float time_s;
    bool is_cq;
    bool is_to_me;
};

void ui_init(bool display_only = false);
void ui_set_waterfall_row(int row, const uint8_t* bins, int len);
// Push a new row from the RX audio pipeline.  Suppressed during TX (see ui_set_rx_waterfall_muted).
void ui_push_waterfall_row(const uint8_t* bins, int len);
// Push a TX tone marker row — always visible regardless of mute state.
void ui_push_tx_waterfall_row(const uint8_t* bins, int len);
// Mute/unmute incoming RX waterfall rows.  Call with true when TX starts, false when TX ends.
void ui_set_rx_waterfall_muted(bool muted);
void ui_clear_waterfall();
void ui_draw_waterfall();
void ui_draw_waterfall_if_dirty();
bool ui_waterfall_dirty();
void ui_draw_countdown(float fraction, bool even_slot, int offset_hz);  // 0.0-1.0 fill of the countdown bar
void ui_set_rx_list(const std::vector<UiRxLine>& lines);
// Zero-heap RX list setter — preferred when callers use RxDecodeEntry directly.
void ui_set_rx_list_static(const RxDecodeEntry* entries, int count);
// Copy a single RX entry by index (for touch handler, thread-safe via disp mutex).
// Returns true if idx is valid and out was populated.
bool ui_get_rx_entry(int idx, RxDecodeEntry* out);
// Current RX list count.
int ui_get_rx_count();
void ui_set_paused(bool paused);
bool ui_is_paused();
void ui_draw_rx(int flash_index = -1);
void ui_force_redraw_rx();
// Colors: pass same-length slot_colors (0 even->green, 1 odd->red) for next/queue
void ui_draw_tx(const std::string& next, const std::vector<std::string>& queue, int page, int selected, const std::vector<bool>& mark_delete, const std::vector<int>& slot_colors = {});
// Replace the six RX text rows during TX. Waterfall/countdown are unchanged.
void ui_draw_tx_hud(const char* tx_text, int voltage_mv, int percent, bool warn, bool writes_blocked);
// Returns selected absolute index or -1 if none
int ui_handle_rx_key(char c);
// Generic list draw (6 lines per page)
void ui_draw_list(const std::vector<std::string>& lines, int page, int highlight_abs = -1);
void ui_draw_debug(const std::vector<std::string>& lines, int page);
// Returns the currently rendered text rows (exact strings drawn for lines 1..6).
void ui_get_visible_text_lines(std::vector<std::string>& out);
// Override one mirrored row for custom render paths outside ui.cpp (e.g. STATUS).
void ui_set_visible_text_line(int row_idx, const std::string& text);
// RX paging info (1-based current page, total pages >= 1).
void ui_get_rx_page_info(int& current_page, int& total_pages);
