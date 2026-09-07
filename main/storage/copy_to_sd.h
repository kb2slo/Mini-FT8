#pragma once

#include <cstdint>
#include <string>

#include "copy_block.h"
#include "storage_service.h"

enum class CopyToSdPress {
    RedrawMenu,
    Dialog,
};

CopyToSdPress copy_to_sd_press(const CopyBlockInputs& in,
                               const std::string& qso_name,
                               const std::string& rt_name,
                               std::int64_t now_ms);

bool copy_to_sd_dialog_active();
bool copy_to_sd_tick(std::int64_t now_ms);
const char* copy_to_sd_menu_item(std::int64_t now_ms);
int copy_to_sd_flash_abs(std::int64_t now_ms);
