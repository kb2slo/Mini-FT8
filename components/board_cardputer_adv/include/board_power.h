#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    int voltage_mv;
    int percent;
    bool charging_known;
    bool charging;
    // GPIO10 is on BAT+ through 100k/100k. The side switch isolates the pack
    // from the charge path, not from this divider, so this is often still true
    // with the switch OFF.
    bool pack_present;
    // Stop new TX/beacon. Independent of flash-write gating.
    bool halted;
    bool writes_blocked;
    bool warn;
} board_power_status_t;

esp_err_t board_power_init(void);
esp_err_t board_power_read(board_power_status_t* out_status);

// Charge Mode stripe: "NN %" when GPIO10 looks like a pack, else "SW ON to charge".
// The ADV side switch does not remove BAT+ from the divider, so this is not a
// reliable switch-position detector.
void board_power_format_charge_stripe(const board_power_status_t* status, char* buf, size_t buf_len);

// Last computed TX-halt state (false until the first successful read).
bool board_power_halted(void);

// Last computed flash-write gate (stricter and slower than TX halt).
bool board_power_writes_blocked(void);

#ifdef __cplusplus
}
#endif
