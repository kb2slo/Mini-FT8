#include "copy_to_sd.h"

#include <cstdio>
#include <string>
#include <utility>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "copy_menu.h"
#include "storage_service.h"
#include "ui.h"

extern void debug_log_line_public(const std::string& msg);

namespace {

const char* kTag = "copy_to_sd";
constexpr uint32_t kWorkerStackBytes = 20480;
constexpr UBaseType_t kWorkerPriority = 3;

SemaphoreHandle_t s_mu = nullptr;
SemaphoreHandle_t s_work = nullptr;
SemaphoreHandle_t s_done = nullptr;
TaskHandle_t s_task = nullptr;
std::string s_priority;
std::string s_priority2;
StorageCopyResult s_result {};
bool s_running = false;
CopyMenuState s_menu;
std::int64_t s_bar_origin_ms = 0;

void copy_to_sd_task(void* /*param*/)
{
    for (;;) {
        xSemaphoreTake(s_work, portMAX_DELAY);
        std::string a;
        std::string b;
        xSemaphoreTake(s_mu, portMAX_DELAY);
        a.swap(s_priority);
        b.swap(s_priority2);
        xSemaphoreGive(s_mu);
        StorageCopyResult result = storage_copy_all_to_sd(a, b);
        xSemaphoreTake(s_mu, portMAX_DELAY);
        s_result = std::move(result);
        s_running = false;
        xSemaphoreGive(s_mu);
        xSemaphoreGive(s_done);
    }
}

bool ensure_worker()
{
    if (s_task != nullptr) {
        return true;
    }
    if (s_mu == nullptr) {
        s_mu = xSemaphoreCreateMutex();
        s_work = xSemaphoreCreateBinary();
        s_done = xSemaphoreCreateBinary();
    }
    if (s_mu == nullptr || s_work == nullptr || s_done == nullptr) {
        ESP_LOGE(kTag, "Failed to create copy primitives");
        return false;
    }
    const BaseType_t ok = xTaskCreatePinnedToCore(
        copy_to_sd_task, "copy_to_sd", kWorkerStackBytes, nullptr, kWorkerPriority, &s_task,
        0);
    if (ok != pdPASS) {
        s_task = nullptr;
        ESP_LOGE(kTag, "Failed to create copy task");
        return false;
    }
    return true;
}

bool begin_copy(const std::string& priority_file, const std::string& priority_file2)
{
    if (!ensure_worker()) {
        return false;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    if (s_running) {
        xSemaphoreGive(s_mu);
        return false;
    }
    s_priority = priority_file;
    s_priority2 = priority_file2;
    s_running = true;
    xSemaphoreGive(s_mu);
    xSemaphoreGive(s_work);
    return true;
}

bool take_result(StorageCopyResult* out)
{
    if (s_done == nullptr || out == nullptr) {
        return false;
    }
    if (xSemaphoreTake(s_done, 0) != pdTRUE) {
        return false;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    *out = s_result;
    xSemaphoreGive(s_mu);
    return true;
}

std::string menu_line_for_result(const StorageCopyResult& result)
{
    switch (result.status) {
        case StorageCopyStatus::SD_MOUNT_FAILED:
            return "SD mount fail";
        case StorageCopyStatus::RADIO_BUSY:
            return "Radio busy";
        case StorageCopyStatus::STORAGE_BUSY:
            return "Storage busy";
        case StorageCopyStatus::LIST_FAILED:
            return "List failed";
        case StorageCopyStatus::OK:
            return "Copied OK";
        case StorageCopyStatus::COPY_FAILED:
            if (result.missed_count == 1 && !result.missed_files.empty()) {
                const std::string& path = result.missed_files.front();
                const size_t slash = path.find_last_of("/\\");
                std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
                if (name.size() > 14) {
                    name.resize(13);
                    name.push_back('>');
                }
                return std::string("Fail ") + name;
            }
            return std::string("Missed ") + std::to_string(result.missed_count) + ", see log";
    }
    return "Copy failed";
}

void log_result(const StorageCopyResult& result)
{
    char log_msg[64];
    snprintf(log_msg, sizeof(log_msg), "Copy SD C%d M%d", result.copied_count, result.missed_count);
    debug_log_line_public(log_msg);
    if (result.err == ESP_OK) {
        debug_log_line_public("Copied storage files to SD");
    } else if (!result.missed_files.empty()) {
        debug_log_line_public(std::string("Copy fail: ") + result.missed_files.front());
    }
}

void show_blocked(const char* text, std::int64_t now_ms)
{
    copy_menu_set_message(&s_menu, text, now_ms);
    debug_log_line_public(std::string("Copy SD ") + text);
}

}  // namespace

CopyToSdPress copy_to_sd_press(const CopyBlockInputs& in,
                               const std::string& qso_name,
                               const std::string& rt_name,
                               std::int64_t now_ms)
{
    ESP_LOGI(kTag,
             "request streams=%u tx=%d decode=%d audio=%d host_bin=%d owns=%d",
             static_cast<unsigned>(in.open_streams),
             in.tx_active,
             in.decode_active,
             in.audio_streaming,
             in.host_bin_active,
             in.firmware_owns);

    const CopyBlockReason why = copy_block_reason(in);
    if (const char* msg = copy_menu_block_message(why)) {
        ESP_LOGW(kTag, "copy blocked: %s", msg);
        show_blocked(msg, now_ms);
        return CopyToSdPress::RedrawMenu;
    }
    if (!begin_copy(qso_name, rt_name)) {
        ESP_LOGW(kTag, "copy blocked: worker busy");
        show_blocked("Storage busy", now_ms);
        return CopyToSdPress::RedrawMenu;
    }
    s_menu.dialog_active = true;
    s_bar_origin_ms = now_ms;
    ui_draw_busy_dialog("Copying to SD", 0, true);
    return CopyToSdPress::Dialog;
}

bool copy_to_sd_dialog_active()
{
    return s_menu.dialog_active;
}

bool copy_to_sd_tick(std::int64_t now_ms)
{
    if (!s_menu.dialog_active) {
        return false;
    }
    const std::int64_t elapsed = (now_ms > s_bar_origin_ms) ? (now_ms - s_bar_origin_ms) : 0;
    const int cycle = static_cast<int>((elapsed / 2) % 2000);
    const int permille = (cycle <= 1000) ? cycle : (2000 - cycle);
    ui_draw_busy_dialog("Copying to SD", permille, false);
    StorageCopyResult result;
    if (!take_result(&result)) {
        return false;
    }
    copy_menu_set_message(&s_menu, menu_line_for_result(result).c_str(), now_ms);
    log_result(result);
    return true;
}

const char* copy_to_sd_menu_item(std::int64_t now_ms)
{
    return copy_menu_item_text(&s_menu, now_ms);
}

int copy_to_sd_flash_abs(std::int64_t now_ms)
{
    return copy_menu_flash_abs(&s_menu, now_ms);
}
