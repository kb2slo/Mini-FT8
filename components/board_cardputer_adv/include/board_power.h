#pragma once

#include <stdint.h>
#include <stdbool.h>
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
    // Stop new TX/beacon. Independent of flash-write gating.
    bool halted;
    bool writes_blocked;
    bool warn;
} board_power_status_t;

esp_err_t board_power_init(void);
esp_err_t board_power_read(board_power_status_t* out_status);

// Last computed TX-halt state (false until the first successful read).
bool board_power_halted(void);

// Last computed flash-write gate (stricter and slower than TX halt).
bool board_power_writes_blocked(void);

#ifdef __cplusplus
}
#endif
