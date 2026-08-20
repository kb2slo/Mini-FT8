#include "file_list_worker.h"

#include <utility>
#include <vector>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "file_list_queue.h"
#include "storage_service.h"

namespace {

const char* kTag = "file_list";
constexpr uint32_t kWorkerStackBytes = 8192;
constexpr UBaseType_t kWorkerPriority = 3;

FileListQueue s_q;
SemaphoreHandle_t s_mu = nullptr;
SemaphoreHandle_t s_work = nullptr;
SemaphoreHandle_t s_idle = nullptr;
TaskHandle_t s_task = nullptr;
FileListKind s_run_kind = FileListKind::QsoDaily;
uint32_t s_run_gen = 0;
std::vector<FileListDone> s_done;
uint32_t s_fallback_gen = 0;

FileListFail fail_from_owner(StorageOwner owner)
{
    switch (owner) {
        case StorageOwner::USB_HOST:
        case StorageOwner::TRANSITION:
            return FileListFail::Busy;
        case StorageOwner::UNAVAILABLE:
            return FileListFail::Unavailable;
        case StorageOwner::FIRMWARE:
            return FileListFail::Failed;
    }
    return FileListFail::Failed;
}

FileListDone run_list(FileListKind kind, uint32_t gen)
{
    FileListDone done;
    done.kind = kind;
    done.gen = gen;
    done.fail = FileListFail::None;
    if (!storage_file_list(done.names)) {
        done.fail = fail_from_owner(storage_service_owner());
        done.names.clear();
        ESP_LOGW(kTag, "list failed kind=%d fail=%d",
                 static_cast<int>(kind), static_cast<int>(done.fail));
    } else {
        ESP_LOGI(kTag, "list kind=%d files=%u",
                 static_cast<int>(kind),
                 static_cast<unsigned>(done.names.size()));
    }
    return done;
}

void push_done(FileListDone done)
{
    s_done.push_back(std::move(done));
}

void file_list_task(void* /*param*/)
{
    for (;;) {
        xSemaphoreTake(s_work, portMAX_DELAY);
        FileListKind kind = FileListKind::QsoDaily;
        uint32_t gen = 0;
        xSemaphoreTake(s_mu, portMAX_DELAY);
        kind = s_run_kind;
        gen = s_run_gen;
        xSemaphoreGive(s_mu);

        FileListDone done = run_list(kind, gen);

        xSemaphoreTake(s_mu, portMAX_DELAY);
        push_done(std::move(done));
        FileListKind next_kind = FileListKind::QsoDaily;
        uint32_t next_gen = 0;
        const bool more = file_list_queue_complete(&s_q, &next_kind, &next_gen);
        if (more) {
            s_run_kind = next_kind;
            s_run_gen = next_gen;
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

void file_list_worker_init()
{
    file_list_queue_init(&s_q);
    s_mu = xSemaphoreCreateMutex();
    s_work = xSemaphoreCreateBinary();
    s_idle = xSemaphoreCreateBinary();
    if (s_mu == nullptr || s_work == nullptr || s_idle == nullptr) {
        ESP_LOGE(kTag, "Failed to create file list primitives");
        return;
    }
    const BaseType_t ok = xTaskCreatePinnedToCore(
        file_list_task, "file_list", kWorkerStackBytes, nullptr, kWorkerPriority, &s_task,
        0);
    if (ok != pdPASS) {
        s_task = nullptr;
        ESP_LOGE(kTag, "Failed to create file list task");
    }
}

uint32_t file_list_worker_submit(FileListKind kind)
{
    if (s_mu == nullptr) {
        const uint32_t gen = ++s_fallback_gen;
        push_done(run_list(kind, gen));
        return gen;
    }

    if (s_task == nullptr) {
        uint32_t gen = 0;
        xSemaphoreTake(s_mu, portMAX_DELAY);
        gen = ++s_fallback_gen;
        xSemaphoreGive(s_mu);
        FileListDone done = run_list(kind, gen);
        xSemaphoreTake(s_mu, portMAX_DELAY);
        push_done(std::move(done));
        xSemaphoreGive(s_mu);
        return gen;
    }

    uint32_t gen = 0;
    FileListKind start_kind = kind;
    bool start_now = false;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    start_now = file_list_queue_submit(&s_q, kind, &gen, &start_kind);
    if (start_now) {
        s_run_kind = start_kind;
        s_run_gen = gen;
    }
    xSemaphoreGive(s_mu);
    if (start_now) {
        xSemaphoreGive(s_work);
    }
    return gen;
}

bool file_list_worker_take(FileListDone* out)
{
    if (out == nullptr || s_mu == nullptr) {
        if (out != nullptr && s_mu == nullptr && !s_done.empty()) {
            *out = std::move(s_done.front());
            s_done.erase(s_done.begin());
            return true;
        }
        return false;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    if (s_done.empty()) {
        xSemaphoreGive(s_mu);
        return false;
    }
    *out = std::move(s_done.front());
    s_done.erase(s_done.begin());
    xSemaphoreGive(s_mu);
    return true;
}

void file_list_worker_flush()
{
    if (s_mu == nullptr || s_idle == nullptr) {
        return;
    }
    for (;;) {
        xSemaphoreTake(s_mu, portMAX_DELAY);
        const bool busy = file_list_queue_busy(&s_q);
        xSemaphoreGive(s_mu);
        if (!busy) {
            return;
        }
        xSemaphoreTake(s_idle, portMAX_DELAY);
    }
}
