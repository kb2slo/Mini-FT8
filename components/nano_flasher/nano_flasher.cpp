#include "nano_flasher.h"

#include <cstring>

#include "esp_log.h"

#include "esp_loader.h"
#include "esp32_usb_cdc_acm_port.h"
#include "usb/cdc_acm_host.h"

static const char* TAG = "nano_flasher";

#if NANO_FLASHER_HAVE_FIRMWARE
#include "nano_target_firmware.h"
#endif

bool nano_flasher_has_firmware(void) {
    return NANO_FLASHER_HAVE_FIRMWARE;
}

#if NANO_FLASHER_HAVE_FIRMWARE

namespace {

struct FlashImage {
    const uint8_t* data;
    uint32_t size;
    uint32_t addr;
    const char* name;
};

esp_loader_error_t flash_image(esp_loader_t* loader, const FlashImage& img) {
    ESP_LOGI(TAG, "Flashing %s (%u bytes @ 0x%06x)", img.name, (unsigned)img.size, (unsigned)img.addr);

    static uint8_t payload[1024];
    esp_loader_flash_cfg_t cfg = {};
    cfg.offset = img.addr;
    cfg.image_size = img.size;
    cfg.block_size = sizeof(payload);

    esp_loader_error_t err = esp_loader_flash_start(loader, &cfg);
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "flash_start(%s): %d", img.name, err);
        return err;
    }

    uint32_t written = 0;
    while (written < img.size) {
        const uint32_t to_write = (img.size - written < sizeof(payload)) ? (img.size - written) : sizeof(payload);
        memcpy(payload, img.data + written, to_write);
        err = esp_loader_flash_write(loader, &cfg, payload, to_write);
        if (err != ESP_LOADER_SUCCESS) {
            ESP_LOGE(TAG, "flash_write(%s) at %u: %d", img.name, (unsigned)written, err);
            return err;
        }
        written += to_write;
    }

    err = esp_loader_flash_finish(loader, &cfg);
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "flash_finish(%s): %d", img.name, err);
        return err;
    }
    ESP_LOGI(TAG, "%s OK", img.name);
    return ESP_LOADER_SUCCESS;
}

}  // namespace

esp_err_t nano_flasher_flash_embedded(uint16_t vid, uint16_t pid) {
    esp_err_t err = cdc_acm_host_install(nullptr);
    const bool installed_by_us = (err == ESP_OK);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "cdc_acm_host_install: %s", esp_err_to_name(err));
        return err;
    }

    esp32_usb_cdc_acm_port_t port = {};
    port.port.ops = &esp32_usb_cdc_acm_ops;
    port.device_vid = vid;
    port.device_pid = pid;
    port.connection_timeout_ms = 1000;
    port.out_buffer_size = 4096;

    esp_loader_t loader = {};
    esp_err_t result = ESP_OK;

    if (esp_loader_init_serial(&loader, &port.port) != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "esp_loader_init_serial failed (VID:0x%04x PID:0x%04x)", vid, pid);
        result = ESP_FAIL;
        goto done;
    }

    {
        esp_loader_connect_args_t connect_args = ESP_LOADER_CONNECT_DEFAULT();
        if (esp_loader_connect(&loader, &connect_args) != ESP_LOADER_SUCCESS) {
            ESP_LOGE(TAG, "esp_loader_connect failed");
            result = ESP_FAIL;
            goto deinit;
        }
        ESP_LOGI(TAG, "Connected, target chip=%d", (int)esp_loader_get_target(&loader));

        const FlashImage images[] = {
            {nano_bootloader_bin, nano_bootloader_bin_len, 0x0, "bootloader"},
            {nano_partition_table_bin, nano_partition_table_bin_len, 0x8000, "partition-table"},
            {nano_companion_bin, nano_companion_bin_len, 0x10000, "nano_companion app"},
        };
        for (const auto& img : images) {
            if (flash_image(&loader, img) != ESP_LOADER_SUCCESS) {
                result = ESP_FAIL;
                goto deinit;
            }
        }
        ESP_LOGI(TAG, "Nano flash complete");
    }

deinit:
    esp_loader_deinit(&loader);
done:
    if (installed_by_us) {
        cdc_acm_host_uninstall();
    }
    return result;
}

#else  // !NANO_FLASHER_HAVE_FIRMWARE

esp_err_t nano_flasher_flash_embedded(uint16_t /*vid*/, uint16_t /*pid*/) {
    ESP_LOGE(TAG, "No Nano firmware staged — run tools/stage_nano_firmware.sh before building");
    return ESP_ERR_NOT_FOUND;
}

#endif
