#pragma once

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

// Periodic pump — call every main-loop tick.
void porta_tick();
