#include "nano_flasher.h"

#include <cstdio>
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

// esp_app_desc_t layout (esp_app_format/include/esp_app_desc.h): magic_word
// (4) + secure_version (4) + reserv1[2] (8) + version[32] + project_name[32].
// Offset within the app image verified empirically against a real sidekick
// build (RFC 0001 §5.2b) — re-derive if target/IDF-version/partition-table/
// secure-boot ever change, don't assume it still holds.
constexpr uint32_t kAppDescOffset = 0x20;
constexpr uint32_t kVersionFieldOffset = kAppDescOffset + 16;
constexpr uint32_t kProjectNameFieldOffset = kAppDescOffset + 48;
constexpr uint32_t kAppFlashBase = 0x10000;  // app partition start, both sides
constexpr size_t kFieldLen = 32;
constexpr char kExpectedProjectName[] = "sidekick";

// Reads a 32-byte esp_app_desc_t field at absolute flash address `addr`
// (via the ROM bootloader, same session as flashing) into a null-terminated
// buffer of at least kFieldLen + 1 bytes.
esp_loader_error_t read_field(esp_loader_t* loader, uint32_t addr, char* out) {
    uint8_t buf[kFieldLen] = {};
    esp_loader_error_t err = esp_loader_flash_read(loader, buf, addr, sizeof(buf));
    if (err != ESP_LOADER_SUCCESS) {
        return err;
    }
    memcpy(out, buf, kFieldLen);
    out[kFieldLen] = '\0';
    return ESP_LOADER_SUCCESS;
}

// The embedded sidekick_bin's own version field — what "up to date" means.
void local_version(char* out) {
    memcpy(out, sidekick_bin + kVersionFieldOffset, kFieldLen);
    out[kFieldLen] = '\0';
}

}  // namespace

esp_err_t nano_flasher_flash_embedded(uint16_t vid, uint16_t pid,
                                       nano_flasher_status_t* out_status,
                                       char* out_remote_version, size_t out_remote_version_size) {
    if (out_status) {
        *out_status = NANO_FLASHER_STATUS_UNKNOWN;
    }
    if (out_remote_version && out_remote_version_size) {
        out_remote_version[0] = '\0';
    }

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
        const target_chip_t target = esp_loader_get_target(&loader);
        ESP_LOGI(TAG, "Connected, target chip=%d", (int)target);
        if (target != ESP32C6_CHIP) {
            // Passive USB-C classification (usb_c_presence, VID/PID only) is
            // a heuristic and can false-positive on another Espressif-VID
            // device (confirmed on real hardware: an AtomS3 was briefly
            // misclassified as a Nano before that was tightened). This is
            // the actual identity check — the ROM bootloader's own reported
            // chip family — and it gates the write, not just the dialog:
            // never flash sidekick firmware onto anything but a real C6.
            ESP_LOGW(TAG, "Not an ESP32-C6 (chip=%d) — refusing to flash", (int)target);
            result = ESP_FAIL;
            goto deinit;
        }

        // Read-then-decide before writing anything (RFC 0001 §5.2b). Logged
        // step by step (not a single collapsed boolean) so a field failure
        // is visible rather than silently falling through to "not
        // installed" — that fallthrough is intentional for a genuinely
        // unrecognized Nano, but indistinguishable from a read bug unless
        // each step's own result is on record.
        char remote_project_name[kFieldLen + 1] = {};
        char remote_version[kFieldLen + 1] = {};
        bool recognized = false;

        esp_loader_error_t name_err = read_field(&loader, kAppFlashBase + kProjectNameFieldOffset, remote_project_name);
        if (name_err != ESP_LOADER_SUCCESS) {
            ESP_LOGW(TAG, "project_name read failed: err=%d", name_err);
        } else if (strcmp(remote_project_name, kExpectedProjectName) != 0) {
            ESP_LOGI(TAG, "project_name read OK but not ours: '%s'", remote_project_name);
        } else {
            esp_loader_error_t ver_err = read_field(&loader, kAppFlashBase + kVersionFieldOffset, remote_version);
            if (ver_err != ESP_LOADER_SUCCESS) {
                ESP_LOGW(TAG, "version read failed: err=%d (project_name matched)", ver_err);
            } else {
                ESP_LOGI(TAG, "project_name='%s' version='%s'", remote_project_name, remote_version);
                recognized = true;
            }
        }

        if (recognized) {
            if (out_remote_version) {
                strncpy(out_remote_version, remote_version, out_remote_version_size - 1);
            }
            char local_ver[kFieldLen + 1] = {};
            local_version(local_ver);
            if (strcmp(remote_version, local_ver) == 0) {
                ESP_LOGI(TAG, "Already up to date (version=%s)", remote_version);
                if (out_status) {
                    *out_status = NANO_FLASHER_STATUS_UP_TO_DATE;
                }
                goto deinit;  // nothing to write
            }
            ESP_LOGI(TAG, "Update available: remote=%s local=%s", remote_version, local_ver);
        } else {
            ESP_LOGI(TAG, "Not recognized as %s — treating as unflashed", kExpectedProjectName);
        }

        const FlashImage images[] = {
            {nano_bootloader_bin, nano_bootloader_bin_len, 0x0, "bootloader"},
            {nano_partition_table_bin, nano_partition_table_bin_len, 0x8000, "partition-table"},
            {sidekick_bin, sidekick_bin_len, 0x10000, "sidekick app"},
        };
        for (const auto& img : images) {
            if (flash_image(&loader, img) != ESP_LOADER_SUCCESS) {
                result = ESP_FAIL;
                goto deinit;
            }
        }
        ESP_LOGI(TAG, "Nano flash complete");
        if (out_status) {
            *out_status = NANO_FLASHER_STATUS_UPDATED;
        }
    }

deinit:
    esp_loader_deinit(&loader);
done:
    if (installed_by_us) {
        cdc_acm_host_uninstall();
    }
    return result;
}

bool nano_flasher_embedded_version(char* out, size_t out_size) {
    if (!out || out_size < kFieldLen + 1) {
        return false;
    }
    local_version(out);
    return true;
}

#else  // !NANO_FLASHER_HAVE_FIRMWARE

esp_err_t nano_flasher_flash_embedded(uint16_t /*vid*/, uint16_t /*pid*/,
                                       nano_flasher_status_t* out_status,
                                       char* /*out_remote_version*/, size_t /*out_remote_version_size*/) {
    if (out_status) {
        *out_status = NANO_FLASHER_STATUS_UNKNOWN;
    }
    ESP_LOGE(TAG, "No Nano firmware staged — run tools/stage_nano_firmware.sh before building");
    return ESP_ERR_NOT_FOUND;
}

bool nano_flasher_embedded_version(char* /*out*/, size_t /*out_size*/) {
    return false;
}

#endif
