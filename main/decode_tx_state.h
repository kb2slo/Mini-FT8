#pragma once

// ============================================================================
// decode_tx_state.h
//
// Shared state between the slot loop in main.cpp (core 0) and the audio /
// decode pipeline in main/audio/ (core 1). Defined in main.cpp; declared here
// once so both sides agree on the type.
//
// This is an inventory of coupling, not an API. Before this header the audio
// pipeline reached back into main.cpp through hand-typed `extern` lines in its
// own .cpp files, so a type drift between definition and use would have been a
// silent ODR violation the linker could not catch. main.cpp includes this
// header too, which is the point: the compiler now checks every definition
// against its declaration.
//
// The list below is the whole remaining surface. Shrinking it is the goal --
// the decode pipeline writing the slot loop's state machine directly is the
// coupling, and a real interface is the fix. Do not add to it without saying
// why in the commit message.
// ============================================================================

#include <cstdint>

// --- Decode configuration (read by the pipeline, set from the UI) -----------
extern bool    g_decode_enabled;
extern int     g_time_osr;
extern int     g_freq_osr;

// --- Decode progress (written by the audio task, read by the slot loop) -----
// g_decode_slot_idx is tagged at decode trigger so RX lines carry slot parity.
// g_decode_applied_slot_idx enforces the sequential invariant "TX in slot N is
// blocked until the decode for slot N-1 has been applied to autoseq state."
extern int64_t          g_decode_slot_idx;
extern volatile int64_t g_decode_applied_slot_idx;
extern volatile bool    g_decode_in_progress;

// --- TX state (written by the slot loop, read by the pipeline) --------------
// g_was_txing latches across the TX->RX transition so the pipeline can discard
// the audio captured while the radio was keyed.
extern volatile bool g_tx_active;
extern volatile bool g_was_txing;

// --- Stream state ----------------------------------------------------------
extern bool          g_streaming;
extern volatile bool g_cdc_initial_sync_pending;
