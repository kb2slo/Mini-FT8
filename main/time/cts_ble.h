#pragma once

#include <sys/time.h>

#include "esp_err.h"

enum class CtsBleState {
    Idle = 0,
    Starting,
    Advertising,
    Connected,
    Reading,
    Success,
    Failed,
    Stopping,
};

esp_err_t cts_ble_start_iphone(const char* adv_name);
void cts_ble_abort(void);
void cts_ble_poll(void);
CtsBleState cts_ble_state(void);
const char* cts_ble_menu_item(void);
bool cts_ble_host_up(void);
bool cts_ble_take_result(struct timeval* tv);
bool cts_ble_ui_dirty(void);
