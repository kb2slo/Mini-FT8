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
    // Soft halt: voltage is too low to risk flash writes / new TX.
    bool halted;
    bool warn;
} board_power_status_t;

esp_err_t board_power_init(void);
esp_err_t board_power_read(board_power_status_t* out_status);

// Last computed halt state (false until the first successful read).
bool board_power_halted(void);

#ifdef __cplusplus
}
#endif
