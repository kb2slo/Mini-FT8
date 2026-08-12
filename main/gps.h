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

// Start GPS parser on the selected UART/pins.
void gps_start(const gps_pins_t& pins);

// Stop GPS parser and release the selected UART.
void gps_stop();

// Periodic housekeeping hook (lightweight; safe to call each loop).
void gps_tick();

// Current state snapshot.
gps_state_t gps_get_state();

// One-shot event: returns true once when auto-baud locks to a new baud.
bool gps_take_baud_update(int* out_baud);
