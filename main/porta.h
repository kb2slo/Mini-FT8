#pragma once

#include <stdint.h>

// ============================================================================
// porta.h — the sidekick link on Grove G1/G2 (UART1).
//
// This port used to be shared. A GPS puck and the sidekick were either/or on
// the same two pins with no presence detection, so the module listened to the
// byte stream and guessed: NMEA sentences meant GPS, a 0xC6-prefixed frame
// meant the sidekick, and it probed two baud rates until one of them produced
// a checksum-valid frame. All of that is gone. Grove GPS was dropped in favour
// of the GNSS/LoRa cap's own receiver, so the port has exactly one possible
// occupant and needs no arbitration, no role, and no baud probe.
//
// The reason it had to go is not tidiness. Choosing the cap as the GPS source
// meant porta_start() was never called at all, so the sidekick link simply did
// not exist -- and in a headless build (I28) that would have removed the only
// UI, along with the only control capable of putting it back.
//
// What remains is the companion beacon (RFC 0001 §5.2c): the sidekick sends
// sync + version[32] + XOR checksum once a second, and the ADV reports whether
// that version matches the sidekick image it carries. I28a replaces this with
// a real bidirectional protocol; until then it is the whole conversation.
// ============================================================================

// Brings up UART1 on G1/G2 at the sidekick's fixed 115200 and starts listening
// for beacons. A no-op if already running.
void porta_start();

// Releases the UART.
void porta_stop();

// Periodic pump — call every main-loop tick. Drains the outbound queue as well
// as reading beacons.
void porta_tick();

// ---------------------------------------------------------------------------
// Outbound events (I28a). Queued, never sent inline: the callers are the slot
// loop and the log path, and neither can afford to wait on a UART. The queue is
// bounded and **drops the oldest** when full rather than blocking or growing --
// a lost log line costs nothing, a missed TX window costs a QSO.
//
// Silently a no-op until porta_start() has run, so callers need no guard.
// ---------------------------------------------------------------------------

// One line of the on-screen log.
void porta_emit_log(const char* text);

// One decoded message. `dt_s` is the decoder's time offset in seconds.
void porta_emit_decode(const char* text, int snr, int offset_hz, float dt_s,
                       bool is_cq, bool is_to_me, bool is_recent_qso);

// Frames dropped because the queue was full — the number that matters when the
// browser's view looks thinner than the screen's.
uint32_t porta_dropped_events();
