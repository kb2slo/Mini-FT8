#include <inttypes.h>

#include "esp_log.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "nano_companion";

void app_main(void)
{
    ESP_LOGI(TAG, "Mini-FT8 Nano companion booting (IDF %s)", esp_get_idf_version());

    uint32_t heartbeat = 0;
    while (1) {
        ESP_LOGI(TAG, "alive: %" PRIu32, heartbeat++);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
