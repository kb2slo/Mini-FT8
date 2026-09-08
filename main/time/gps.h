#pragma once

#include <stdint.h>
#include <string>

#include "driver/gpio.h"
#include "driver/uart.h"

struct gps_state_t {
    bool valid_fix = false;
    int satellites = 0;
    std::string time_utc;    // "HH:MM:SS"
    std::string date_utc;    // "YYYY-MM-DD"
    double latitude = 0.0;
    double longitude = 0.0;
    std::string grid_square; // "CM97"
    uint32_t last_rx_ms = 0; // last received decodable NMEA sentence
    int active_baud = 0;
    bool baud_locked = false;
    bool running = false;
};

struct gps_pins_t {
    uart_port_t uart;
    gpio_num_t rx;
    gpio_num_t tx;
    int default_baud;
    bool auto_baud;
    // Cap LoRa-1262 ATGM336H/AT6668: recover UART baud after hosts (e.g. some
    // M5Launcher builds) drive G13/G15 as GPIO outputs and corrupt the module.
    bool casic_baud_recover;
};

// Start GPS parser on UART1 PortA pins with preload baud (9600 or 115200).
void gps_start(int preload_baud);

// Start GPS parser on the selected UART/pins. Owns the UART: installs the
// driver, configures pins/baud, and self-polls in gps_tick().
void gps_start(const gps_pins_t& pins);

// Start GPS parsing state without taking ownership of the UART — for a
// caller (porta.cpp) that already owns the driver and has already resolved
// which baud NMEA is arriving at (its own probe already decoded a valid
// sentence before calling this). gps_tick() will not touch the UART in
// this mode; feed bytes via gps_ingest() instead.
void gps_start_fed(int active_baud);

// Feed bytes already read by an external UART owner (porta.cpp). Only
// valid after gps_start_fed(); a no-op otherwise.
void gps_ingest(const uint8_t* data, int len);

// True if `line` is a well-formed, checksum-valid NMEA sentence
// ("$...*HH"). Exposed so porta.cpp can use the same GPS-recognition test
// during role arbitration that gps.cpp uses internally, rather than a
// second, possibly-drifting implementation.
bool gps_is_valid_nmea_line(const std::string& line);

// Stop GPS parsing and release the UART, if this instance owns it
// (gps_start()) — a no-op on the UART for a gps_start_fed() instance,
// since porta.cpp owns that driver instead.
void gps_stop();

// Periodic housekeeping hook (lightweight; safe to call each loop).
void gps_tick();

// Current state snapshot.
gps_state_t gps_get_state();

// One-shot event: returns true once when auto-baud locks to a new baud.
bool gps_take_baud_update(int* out_baud);
