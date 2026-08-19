#include "station_save_worker.h"

#include <string>
#include <utility>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "core_api_internal.h"
#include "station_save_queue.h"
#include "storage_service.h"

extern void debug_log_line_public(const std::string& msg);

namespace {

const char* kTag = "station_save";
constexpr char kStationFile[] = "Station.txt";
constexpr uint32_t kWorkerStackBytes = 8192;
constexpr UBaseType_t kWorkerPriority = 3;

StationSaveQueue s_q;
SemaphoreHandle_t s_mu = nullptr;
SemaphoreHandle_t s_work = nullptr;
SemaphoreHandle_t s_idle = nullptr;
TaskHandle_t s_task = nullptr;
std::string s_write_blob;

void write_blob_sync(const std::string& blob)
{
    if (!storage_file_write_atomic(kStationFile, blob)) {
        ESP_LOGE(kTag, "Failed to write %s", kStationFile);
        debug_log_line_public("Station write failed");
        return;
    }
    core_fire_config_changed();
}

void station_save_task(void* /*param*/)
{
    for (;;) {
        xSemaphoreTake(s_work, portMAX_DELAY);
        std::string blob;
        xSemaphoreTake(s_mu, portMAX_DELAY);
        blob.swap(s_write_blob);
        xSemaphoreGive(s_mu);

        write_blob_sync(blob);

        std::string next;
        bool more = false;
        xSemaphoreTake(s_mu, portMAX_DELAY);
        more = station_save_queue_complete(&s_q, &next);
        if (more) {
            s_write_blob = std::move(next);
        }
        xSemaphoreGive(s_mu);
        if (more) {
            xSemaphoreGive(s_work);
        } else {
            xSemaphoreGive(s_idle);
        }
    }
}

}  // namespace

void station_save_worker_init()
{
    station_save_queue_init(&s_q);
    s_mu = xSemaphoreCreateMutex();
    s_work = xSemaphoreCreateBinary();
    s_idle = xSemaphoreCreateBinary();
    if (s_mu == nullptr || s_work == nullptr || s_idle == nullptr) {
        ESP_LOGE(kTag, "Failed to create Station save primitives");
        return;
    }
    const BaseType_t ok = xTaskCreatePinnedToCore(
        station_save_task, "station_save", kWorkerStackBytes, nullptr, kWorkerPriority,
        &s_task, 0);
    if (ok != pdPASS) {
        s_task = nullptr;
        ESP_LOGE(kTag, "Failed to create Station save task");
    }
}

void station_save_worker_submit(std::string blob)
{
    if (s_task == nullptr || s_mu == nullptr) {
        write_blob_sync(blob);
        return;
    }

    std::string start;
    bool start_now = false;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    start_now = station_save_queue_submit(&s_q, std::move(blob), &start);
    if (start_now) {
        s_write_blob = std::move(start);
    }
    xSemaphoreGive(s_mu);
    if (start_now) {
        xSemaphoreGive(s_work);
    }
}

void station_save_worker_flush()
{
    if (s_mu == nullptr || s_idle == nullptr) {
        return;
    }
    for (;;) {
        xSemaphoreTake(s_mu, portMAX_DELAY);
        const bool busy = station_save_queue_busy(&s_q);
        xSemaphoreGive(s_mu);
        if (!busy) {
            return;
        }
        xSemaphoreTake(s_idle, portMAX_DELAY);
    }
}
