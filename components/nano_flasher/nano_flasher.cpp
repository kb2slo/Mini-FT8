#include "nano_flasher.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"

#include "driver/uart.h"
#include "esp32_port.h"
#include "esp32_usb_cdc_acm_port.h"
#include "esp_loader.h"
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

// Shared logic once a session is connected, regardless of transport: chip-
// family gate, identity read-then-decide, and the flash-images loop (RFC
// 0001 §5.2b/§5.2c). Only how `loader` got connected differs between the
// USB-C and PORTA UART entry points below. `out_diag` (nullable; short —
// callers with a display-row budget like the PORTA on-screen log should
// keep it under ~16 chars) reports which stage failed — PORTA has no
// serial console fallback the way USB-C does, so unlike a bring-up aid
// this is a lasting diagnostic, not something to trim later.
esp_err_t flash_after_connect(esp_loader_t* loader, nano_flasher_status_t* out_status,
                               char* out_remote_version, size_t out_remote_version_size,
                               char* out_diag, size_t out_diag_size) {
    const target_chip_t target = esp_loader_get_target(loader);
    ESP_LOGI(TAG, "Connected, target chip=%d", (int)target);
    if (target != ESP32C6_CHIP) {
        // Passive presence classification (VID/PID over USB-C, or a beacon
        // over PORTA) is a heuristic and can false-positive on another
        // Espressif device (confirmed on real hardware: an AtomS3 was
        // briefly misclassified as a Nano before that was tightened). This
        // is the actual identity check — the ROM bootloader's own reported
        // chip family — and it gates the write, not just the dialog: never
        // flash sidekick firmware onto anything but a real C6.
        ESP_LOGW(TAG, "Not an ESP32-C6 (chip=%d) — refusing to flash", (int)target);
        if (out_diag) snprintf(out_diag, out_diag_size, "not C6 (%d)", (int)target);
        return ESP_FAIL;
    }

    // Read-then-decide before writing anything. Logged step by step (not a
    // single collapsed boolean) so a field failure is visible rather than
    // silently falling through to "not installed" — that fallthrough is
    // intentional for a genuinely unrecognized Nano, but indistinguishable
    // from a read bug unless each step's own result is on record.
    char remote_project_name[kFieldLen + 1] = {};
    char remote_version[kFieldLen + 1] = {};
    bool recognized = false;

    esp_loader_error_t name_err = read_field(loader, kAppFlashBase + kProjectNameFieldOffset, remote_project_name);
    if (name_err != ESP_LOADER_SUCCESS) {
        ESP_LOGW(TAG, "project_name read failed: err=%d", name_err);
        if (out_diag) snprintf(out_diag, out_diag_size, "name read fail %d", name_err);
    } else if (strcmp(remote_project_name, kExpectedProjectName) != 0) {
        ESP_LOGI(TAG, "project_name read OK but not ours: '%s'", remote_project_name);
    } else {
        esp_loader_error_t ver_err = read_field(loader, kAppFlashBase + kVersionFieldOffset, remote_version);
        if (ver_err != ESP_LOADER_SUCCESS) {
            ESP_LOGW(TAG, "version read failed: err=%d (project_name matched)", ver_err);
            if (out_diag) snprintf(out_diag, out_diag_size, "ver read fail %d", ver_err);
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
            return ESP_OK;  // nothing to write
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
        if (flash_image(loader, img) != ESP_LOADER_SUCCESS) {
            if (out_diag) snprintf(out_diag, out_diag_size, "write fail %s", img.name);
            return ESP_FAIL;
        }
    }
    ESP_LOGI(TAG, "Nano flash complete");
    if (out_status) {
        *out_status = NANO_FLASHER_STATUS_UPDATED;
    }
    return ESP_OK;
}

// PORTA UART port setup (RFC 0001 §5.2c). Deliberately does not use
// esp-serial-flasher's stock esp32_uart_ops as-is: its .init unconditionally
// configures a reset_pin/boot_pin pair as GPIO outputs, for chip-flashes-
// chip setups with wired reset/boot control — which PORTA doesn't have and
// this doesn't fake with a guessed-safe GPIO on the ADV board. sidekick
// enters download mode entirely on its own (its on-board GPIO9 button, held
// through a self-triggered esp_restart() — RFC 0001 §5.2), so this port
// only ever needs the bare UART peripheral: init sets that up and nothing
// else; enter_bootloader/reset_target are no-ops, relying purely on
// esp_loader_connect()'s own sync retries over the wire. Every other op
// (write/read/timers/etc) is reused unmodified from the library's own
// esp32_uart_ops — those don't touch reset_pin/boot_pin at all.
esp_loader_error_t porta_uart_init(esp_loader_port_t* port) {
    esp32_port_t* p = container_of(port, esp32_port_t, port);
    p->_peripheral_needs_deinit = false;
    if (p->dont_initialize_peripheral) {
        return ESP_LOADER_SUCCESS;
    }

    uart_config_t uart_config = {};
    uart_config.baud_rate = (int)p->baud_rate;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    const int rx_buffer_size = p->rx_buffer_size ? (int)p->rx_buffer_size : 400;
    const int tx_buffer_size = p->tx_buffer_size ? (int)p->tx_buffer_size : 400;

    if (uart_param_config((uart_port_t)p->uart_port, &uart_config) != ESP_OK) {
        return ESP_LOADER_ERROR_FAIL;
    }
    if (uart_set_pin((uart_port_t)p->uart_port, (int)p->uart_tx_pin, (int)p->uart_rx_pin,
                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        return ESP_LOADER_ERROR_FAIL;
    }
    if (uart_driver_install((uart_port_t)p->uart_port, rx_buffer_size, tx_buffer_size, 0, nullptr, 0) != ESP_OK) {
        return ESP_LOADER_ERROR_FAIL;
    }
    p->_peripheral_needs_deinit = true;
    return ESP_LOADER_SUCCESS;
}

void porta_uart_noop(esp_loader_port_t*) {}

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
        result = flash_after_connect(&loader, out_status, out_remote_version, out_remote_version_size,
                                      nullptr, 0);
    }

deinit:
    esp_loader_deinit(&loader);
done:
    if (installed_by_us) {
        cdc_acm_host_uninstall();
    }
    return result;
}

esp_err_t nano_flasher_flash_embedded_uart(uart_port_t uart, gpio_num_t tx_pin, gpio_num_t rx_pin,
                                            uint32_t baud_rate,
                                            nano_flasher_status_t* out_status,
                                            char* out_remote_version, size_t out_remote_version_size,
                                            char* out_diag, size_t out_diag_size) {
    if (out_status) {
        *out_status = NANO_FLASHER_STATUS_UNKNOWN;
    }
    if (out_remote_version && out_remote_version_size) {
        out_remote_version[0] = '\0';
    }
    if (out_diag && out_diag_size) {
        out_diag[0] = '\0';
    }

    esp_loader_port_ops_t ops = esp32_uart_ops;
    ops.init = porta_uart_init;
    ops.enter_bootloader = porta_uart_noop;
    ops.reset_target = porta_uart_noop;

    esp32_port_t port = {};
    port.port.ops = &ops;
    port.baud_rate = baud_rate;
    port.uart_port = (uint32_t)uart;
    port.uart_tx_pin = tx_pin;
    port.uart_rx_pin = rx_pin;
    port.reset_pin = GPIO_NUM_NC;  // never touched — see porta_uart_init comment
    port.boot_pin = GPIO_NUM_NC;

    esp_loader_t loader = {};
    esp_err_t result = ESP_OK;

    if (esp_loader_init_serial(&loader, &port.port) != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "esp_loader_init_serial (PORTA UART) failed");
        if (out_diag) snprintf(out_diag, out_diag_size, "no UART init");
        return ESP_FAIL;
    }

    // sidekick isn't reset by anything we control (see header comment) — it
    // has to already be sitting in its ROM bootloader by the time we sync,
    // which takes the operator a few seconds to arrange (hold the button).
    // A generous trial count covers that wait within one connect call
    // rather than needing an outer retry loop around init/deinit too.
    esp_loader_connect_args_t connect_args = {.sync_timeout = 150, .trials = 100};
    if (esp_loader_connect(&loader, &connect_args) != ESP_LOADER_SUCCESS) {
        ESP_LOGW(TAG, "esp_loader_connect (PORTA) failed — Nano not in download mode?");
        if (out_diag) snprintf(out_diag, out_diag_size, "no sync (hold btn?)");
        esp_loader_deinit(&loader);
        return ESP_FAIL;
    }

    result = flash_after_connect(&loader, out_status, out_remote_version, out_remote_version_size,
                                  out_diag, out_diag_size);
    esp_loader_deinit(&loader);
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

esp_err_t nano_flasher_flash_embedded_uart(uart_port_t /*uart*/, gpio_num_t /*tx_pin*/, gpio_num_t /*rx_pin*/,
                                            uint32_t /*baud_rate*/,
                                            nano_flasher_status_t* out_status,
                                            char* /*out_remote_version*/, size_t /*out_remote_version_size*/,
                                            char* /*out_diag*/, size_t /*out_diag_size*/) {
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
