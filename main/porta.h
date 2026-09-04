#pragma once

// PORTA (Cardputer Grove G1/G2, UART1) role arbitration — RFC 0001 §5.2/§5.2b.
//
// GPS puck and companion Nano are either/or on the same two Grove pins, and
// PORTA has no physical presence detection (unlike USB-C's B18 presence
// layer), so the only way to know what is plugged in is to listen: a
// companion's handshake starts with sync byte 0xC6, chosen because it can
// never collide with NMEA's '$'-prefixed sentences, so the byte stream
// itself is self-describing. This module owns UART1 outright, sniffs the
// first recognizable frame, and either hands off to gps.cpp (which keeps
// its existing NMEA-parsing internals unchanged, just fed externally
// instead of self-polling) or — once the companion protocol exists — a
// companion parser. For now, Companion is detect-and-log only; the
// PROBE/VERSION handshake itself is a follow-up milestone.

enum class PortaRole {
    kUnknown,
    kGps,
    kCompanion,
};

// Starts PORTA arbitration on UART1 (G1/G2). `gps_baud_hint` is the
// persisted last-known-good GPS baud (station config) used as the first
// probe attempt; a companion Nano is always found regardless, since it only
// ever speaks 115200. A no-op if already running.
void porta_start(int gps_baud_hint);

// Stops arbitration and, if a role was locked, releases whatever it handed
// off to (gps_stop() for kGps) before releasing the UART itself.
void porta_stop();

// Periodic pump — call every main-loop tick, same as gps_tick() today.
void porta_tick();

PortaRole porta_get_role();

// Only meaningful once porta_get_role() == PortaRole::kCompanion: the
// version string from sidekick's last-validated beacon, and whether it
// matched this ADV's own embedded sidekick build (RFC 0001 §5.2c) — no
// USB-C session needed to know staleness, unlike the nano_flasher path.
// Empty / false before a companion has ever locked.
const char* porta_get_companion_version();
bool porta_companion_version_matches();
