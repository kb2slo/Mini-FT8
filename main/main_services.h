#pragma once

// ============================================================================
// main_services.h
//
// Small services main.cpp provides to other translation units. Defined in
// main.cpp; declared here once so callers and definition cannot drift.
//
// Same rationale as decode_tx_state.h: these were previously hand-typed
// `extern` lines repeated in each consuming .cpp. Unlike that file, this is not
// shared mutable state -- it is the on-screen debug log, a heap trace, and a
// redraw nudge, all of which legitimately belong to whoever owns the display.
// They live here rather than in a `ui_*` module because main.cpp still owns the
// screen; when the UI layer is extracted, these move with it.
// ============================================================================

#include <string>

// Append a line to the on-screen debug log (the `D` screen).
void debug_log_line_public(const std::string& msg);

// Log free / minimum-free / largest-block heap figures under `tag`.
void log_heap(const char* tag);

// Mark config views dirty so the next UI tick redraws whichever of MENU /
// STATUS is showing. Called from the Station.txt save worker task once a
// write lands.
void ui_mark_config_dirty(void);
