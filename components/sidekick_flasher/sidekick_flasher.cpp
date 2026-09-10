#include "sidekick_flasher.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"

#include "driver/uart.h"
#include "esp32_port.h"
#include "esp32_usb_cdc_acm_port.h"
#include "esp_loader.h"
#include "usb/cdc_acm_host.h"

static const char* TAG = "sidekick_flasher";

#if SIDEKICK_FLASHER_HAVE_FIRMWARE
#include "nano_target_firmware.h"
#endif

bool sidekick_flasher_has_firmware(void) {
    return SIDEKICK_FLASHER_HAVE_FIRMWARE;
}

#if SIDEKICK_FLASHER_HAVE_FIRMWARE

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
// Where the app lives is NOT a constant: it moves the moment the sidekick's
// partition table changes (a factory layout puts it at 0x10000, an OTA layout
// at ota_0). Both the version read and the write target are derived from the
// embedded partition-table image instead, so the two sides cannot disagree.
// Hardcoding it meant a partition change on the sidekick would make the ADV
// read the version from the wrong place and then write the app over otadata
// and phy_init -- bricking the part it was trying to install onto.
constexpr uint32_t kPartitionTableFlashOffset = 0x8000;
constexpr size_t   kPartitionEntrySize = 32;
constexpr uint8_t  kPartitionMagic0 = 0xAA;
constexpr uint8_t  kPartitionMagic1 = 0x50;
constexpr uint8_t  kPartitionTypeApp = 0x00;
constexpr uint8_t  kPartitionTypeData = 0x01;
constexpr uint8_t  kPartitionSubtypeOtadata = 0x00;
constexpr uint32_t kPartitionOffsetField = 4;
constexpr uint32_t kPartitionLabelField = 12;
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

// First app partition in the embedded table: its flash offset, and its label
// for logging. Returns 0 if the table does not parse, which callers treat as
// fatal rather than guessing an address.
uint32_t embedded_partition_offset(uint8_t type, int subtype,
                                   char* out_label, size_t out_label_size) {
    for (size_t i = 0; i + kPartitionEntrySize <= sidekick_partition_table_bin_len;
         i += kPartitionEntrySize) {
        const uint8_t* e = sidekick_partition_table_bin + i;
        if (e[0] != kPartitionMagic0 || e[1] != kPartitionMagic1) {
            break;  // end of table (or the MD5 entry); no match
        }
        if (e[2] != type) {
            continue;
        }
        if (subtype >= 0 && e[3] != (uint8_t)subtype) {
            continue;
        }
        if (out_label && out_label_size) {
            snprintf(out_label, out_label_size, "%.*s", 16,
                     (const char*)(e + kPartitionLabelField));
        }
        return (uint32_t)e[kPartitionOffsetField] |
               ((uint32_t)e[kPartitionOffsetField + 1] << 8) |
               ((uint32_t)e[kPartitionOffsetField + 2] << 16) |
               ((uint32_t)e[kPartitionOffsetField + 3] << 24);
    }
    return 0;
}

// First app partition, any subtype: `factory` on the old layout, `ota_0` now.
uint32_t embedded_app_offset(char* out_label, size_t out_label_size) {
    return embedded_partition_offset(kPartitionTypeApp, -1, out_label, out_label_size);
}

// The embedded sidekick_bin's own version field — what "up to date" means.
void local_version(char* out) {
    memcpy(out, sidekick_bin + kVersionFieldOffset, kFieldLen);
    out[kFieldLen] = '\0';
}

// Which chip the embedded image was built for, read from its own ESP image
// header: byte 0 is the 0xE9 magic, bytes 12-13 are a little-endian
// esp_chip_id_t. Derived from the payload rather than hardcoded, so staging a
// firmware built for a different target cannot go unnoticed.
constexpr uint8_t  kEspImageMagic       = 0xE9;
constexpr size_t   kEspImageChipIdOffset = 12;
constexpr uint16_t kEspChipIdEsp32S3    = 9;
constexpr uint16_t kEspChipIdEsp32C6    = 13;

// -1 when the blob does not look like an ESP image at all.
int embedded_chip_id() {
    if (sidekick_bin_len < kEspImageChipIdOffset + 2 || sidekick_bin[0] != kEspImageMagic) {
        return -1;
    }
    return (int)((uint16_t)sidekick_bin[kEspImageChipIdOffset] |
                 ((uint16_t)sidekick_bin[kEspImageChipIdOffset + 1] << 8));
}

// esp_loader's target_chip_t and the image header's esp_chip_id_t are
// different enumerations for the same idea, so they need mapping rather than
// comparing.
int chip_id_for_target(target_chip_t target) {
    switch (target) {
    case ESP32S3_CHIP: return kEspChipIdEsp32S3;
    case ESP32C6_CHIP: return kEspChipIdEsp32C6;
    default:           return -1;
    }
}

// Shared logic once a session is connected, regardless of transport: chip-
// family gate, identity read-then-decide, and the flash-images loop (RFC
// 0001 §5.2b/§5.2c). Only how `loader` got connected differs between the
// USB-C and PORTA UART entry points below. `out_diag` (nullable; short —
// callers with a display-row budget like the PORTA on-screen log should
// keep it under ~16 chars) reports which stage failed — PORTA has no
// serial console fallback the way USB-C does, so unlike a bring-up aid
// this is a lasting diagnostic, not something to trim later.
esp_err_t flash_after_connect(esp_loader_t* loader, bool allow_overwrite, bool probe_only,
                               sidekick_flasher_info_t* out_info,
                               char* out_diag, size_t out_diag_size) {
    const target_chip_t target = esp_loader_get_target(loader);
    ESP_LOGI(TAG, "Connected, target chip=%d", (int)target);

    // One gate, and it is derived from the payload rather than naming a chip.
    // There used to be two: a hardcoded `target != ESP32C6_CHIP` family check
    // and this payload comparison. The hardcoded one went stale the moment the
    // sidekick was retargeted to esp32s3 (I3) -- it then refused every device
    // that could possibly be correct, which is how field-flash quietly stopped
    // working at all. The payload comparison is strictly stronger anyway: it
    // rejects the same wrong-family cases *and* the case the family check
    // could never see, an image built for a different chip than the one on the
    // other end. chip_id_for_target() returns -1 for a family we have no
    // mapping for, which cannot equal any real payload id, so unknown chips
    // are refused without needing to be enumerated.
    const int payload_chip = embedded_chip_id();
    const int target_chip = chip_id_for_target(target);
    if (out_info) {
        out_info->chip_id = target_chip;
    }
    if (payload_chip < 0) {
        ESP_LOGE(TAG, "Embedded firmware is not an ESP image (magic=0x%02x)", sidekick_bin[0]);
        if (out_diag) snprintf(out_diag, out_diag_size, "bad payload");
        return ESP_FAIL;
    }
    if (payload_chip != target_chip) {
        ESP_LOGE(TAG, "Embedded firmware is for chip_id=%d, target is chip_id=%d — refusing to flash",
                 payload_chip, target_chip);
        if (out_diag) snprintf(out_diag, out_diag_size, "fw%d!=hw%d", payload_chip, target_chip);
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

    char app_label[20] = {};
    const uint32_t app_base = embedded_app_offset(app_label, sizeof(app_label));
    if (app_base == 0) {
        ESP_LOGE(TAG, "Embedded partition table has no app partition — refusing to flash");
        if (out_diag) snprintf(out_diag, out_diag_size, "no app part");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "App partition '%s' at 0x%06lx (from the embedded table)",
             app_label, (unsigned long)app_base);

    esp_loader_error_t name_err = read_field(loader, app_base + kProjectNameFieldOffset, remote_project_name);
    if (name_err != ESP_LOADER_SUCCESS) {
        ESP_LOGW(TAG, "project_name read failed: err=%d", name_err);
        if (out_diag) snprintf(out_diag, out_diag_size, "name read fail %d", name_err);
    } else if (strcmp(remote_project_name, kExpectedProjectName) != 0) {
        ESP_LOGI(TAG, "project_name read OK but not ours: '%s'", remote_project_name);
    } else {
        esp_loader_error_t ver_err = read_field(loader, app_base + kVersionFieldOffset, remote_version);
        if (ver_err != ESP_LOADER_SUCCESS) {
            ESP_LOGW(TAG, "version read failed: err=%d (project_name matched)", ver_err);
            if (out_diag) snprintf(out_diag, out_diag_size, "ver read fail %d", ver_err);
        } else {
            ESP_LOGI(TAG, "project_name='%s' version='%s'", remote_project_name, remote_version);
            recognized = true;
        }
    }

    if (out_info) {
        strncpy(out_info->project_name, remote_project_name, sizeof(out_info->project_name) - 1);
        strncpy(out_info->version, remote_version, sizeof(out_info->version) - 1);
    }

    if (recognized) {
        char local_ver[kFieldLen + 1] = {};
        local_version(local_ver);
        if (strcmp(remote_version, local_ver) == 0) {
            ESP_LOGI(TAG, "Already up to date (version=%s)", remote_version);
            if (out_info) out_info->status = SIDEKICK_FLASHER_STATUS_UP_TO_DATE;
            return ESP_OK;  // nothing to write
        }
        ESP_LOGI(TAG, "Update available: remote=%s local=%s", remote_version, local_ver);
        if (out_info) out_info->status = SIDEKICK_FLASHER_STATUS_OUTDATED;
    } else {
        // Not ours, or the descriptor could not be read at all. Those two are
        // deliberately the same outcome: an unreadable descriptor is exactly
        // what an erased factory part looks like, and also exactly what a
        // board we failed to read looks like, and we cannot tell them apart.
        //
        // This used to fall straight through to the write, on the reasoning
        // that an unrecognized C6 was almost certainly a stock NanoC6. That
        // reasoning died with the retarget: an unrecognized S3 is just as
        // likely to be the operator's own board, and overwriting it is not
        // recoverable from the ADV. So it now stops here unless the operator
        // was shown what is there and said yes.
        ESP_LOGI(TAG, "Not recognized as %s (project_name='%s')",
                 kExpectedProjectName, remote_project_name);
        if (out_info) out_info->status = SIDEKICK_FLASHER_STATUS_FOREIGN;
        if (!allow_overwrite) {
            ESP_LOGW(TAG, "Refusing to overwrite an unrecognized device without confirmation");
            if (out_diag) snprintf(out_diag, out_diag_size, "foreign");
            return ESP_OK;  // a decision, not a failure
        }
        ESP_LOGW(TAG, "Overwriting an unrecognized device at the operator's request");
    }

    if (probe_only) {
        return ESP_OK;
    }

    const FlashImage images[] = {
        {sidekick_bootloader_bin, sidekick_bootloader_bin_len, 0x0, "bootloader"},
        {sidekick_partition_table_bin, sidekick_partition_table_bin_len,
         kPartitionTableFlashOffset, "partition-table"},
        {sidekick_bin, sidekick_bin_len, app_base, "sidekick app"},
    };
    // otadata, when the target's table has one — skipped on a single-app
    // layout, which has none. It matters on a part that has already run an
    // OTA: its otadata would still select ota_1 while we have just written
    // ota_0, so it would boot the stale slot. Writing the initial (erased)
    // contents makes the bootloader fall back to the first app partition,
    // which is the one we wrote.
    const uint32_t otadata_base =
        embedded_partition_offset(kPartitionTypeData, kPartitionSubtypeOtadata, nullptr, 0);
    for (const auto& img : images) {
        if (flash_image(loader, img) != ESP_LOADER_SUCCESS) {
            if (out_diag) snprintf(out_diag, out_diag_size, "write fail %s", img.name);
            return ESP_FAIL;
        }
    }
    if (otadata_base != 0) {
        ESP_LOGI(TAG, "Resetting otadata at 0x%06lx", (unsigned long)otadata_base);
        const FlashImage otadata = {sidekick_otadata_bin, sidekick_otadata_bin_len,
                                    otadata_base, "otadata"};
        if (flash_image(loader, otadata) != ESP_LOADER_SUCCESS) {
            if (out_diag) snprintf(out_diag, out_diag_size, "write fail otadata");
            return ESP_FAIL;
        }
    }
    ESP_LOGI(TAG, "Sidekick flash complete");
    if (out_info) out_info->status = SIDEKICK_FLASHER_STATUS_UPDATED;
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

// Shared USB-C session: install the CDC-ACM host if we must, connect to the
// ROM bootloader, run the decide-and-maybe-write logic, tear down. The probe
// and the flash differ only in the two flags they pass through, so they share
// this rather than each keeping their own copy of the teardown ordering --
// which is the part that was got wrong on the first hardware attempt.
esp_err_t usb_session(uint16_t vid, uint16_t pid, bool allow_overwrite, bool probe_only,
                      sidekick_flasher_info_t* out_info,
                      char* out_diag, size_t out_diag_size) {
    if (out_info) {
        *out_info = {};
        out_info->status = SIDEKICK_FLASHER_STATUS_UNKNOWN;
        out_info->chip_id = -1;
    }
    if (out_diag && out_diag_size) {
        out_diag[0] = '\0';
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
        if (out_diag) snprintf(out_diag, out_diag_size, "no serial");
        result = ESP_FAIL;
        goto done;
    }

    {
        esp_loader_connect_args_t connect_args = ESP_LOADER_CONNECT_DEFAULT();
        if (esp_loader_connect(&loader, &connect_args) != ESP_LOADER_SUCCESS) {
            ESP_LOGE(TAG, "esp_loader_connect failed");
            if (out_diag) snprintf(out_diag, out_diag_size, "no connect");
            result = ESP_FAIL;
            goto deinit;
        }
        result = flash_after_connect(&loader, allow_overwrite, probe_only, out_info,
                                     out_diag, out_diag_size);
    }

deinit:
    esp_loader_deinit(&loader);
done:
    if (installed_by_us) {
        cdc_acm_host_uninstall();
    }
    return result;
}

}  // namespace

esp_err_t sidekick_flasher_probe(uint16_t vid, uint16_t pid,
                                 sidekick_flasher_info_t* out_info,
                                 char* out_diag, size_t out_diag_size) {
    return usb_session(vid, pid, /*allow_overwrite=*/false, /*probe_only=*/true,
                       out_info, out_diag, out_diag_size);
}

esp_err_t sidekick_flasher_flash_embedded(uint16_t vid, uint16_t pid, bool allow_overwrite,
                                       sidekick_flasher_info_t* out_info,
                                       char* out_diag, size_t out_diag_size) {
    return usb_session(vid, pid, allow_overwrite, /*probe_only=*/false,
                       out_info, out_diag, out_diag_size);
}

esp_err_t sidekick_flasher_flash_embedded_uart(uart_port_t uart, gpio_num_t tx_pin, gpio_num_t rx_pin,
                                            uint32_t baud_rate, bool allow_overwrite,
                                            sidekick_flasher_info_t* out_info,
                                            char* out_diag, size_t out_diag_size) {
    if (out_info) {
        *out_info = {};
        out_info->status = SIDEKICK_FLASHER_STATUS_UNKNOWN;
        out_info->chip_id = -1;
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

    result = flash_after_connect(&loader, allow_overwrite, /*probe_only=*/false, out_info,
                                 out_diag, out_diag_size);
    esp_loader_deinit(&loader);
    return result;
}

bool sidekick_flasher_embedded_version(char* out, size_t out_size) {
    if (!out || out_size < kFieldLen + 1) {
        return false;
    }
    local_version(out);
    return true;
}

#else  // !SIDEKICK_FLASHER_HAVE_FIRMWARE

esp_err_t sidekick_flasher_probe(uint16_t /*vid*/, uint16_t /*pid*/,
                                 sidekick_flasher_info_t* out_info,
                                 char* /*out_diag*/, size_t /*out_diag_size*/) {
    if (out_info) {
        *out_info = {};
        out_info->status = SIDEKICK_FLASHER_STATUS_UNKNOWN;
        out_info->chip_id = -1;
    }
    ESP_LOGE(TAG, "No sidekick firmware staged — run tools/stage_sidekick_firmware.sh before building");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sidekick_flasher_flash_embedded(uint16_t /*vid*/, uint16_t /*pid*/, bool /*allow_overwrite*/,
                                       sidekick_flasher_info_t* out_info,
                                       char* /*out_diag*/, size_t /*out_diag_size*/) {
    if (out_info) {
        *out_info = {};
        out_info->status = SIDEKICK_FLASHER_STATUS_UNKNOWN;
        out_info->chip_id = -1;
    }
    ESP_LOGE(TAG, "No Nano firmware staged — run tools/stage_sidekick_firmware.sh before building");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sidekick_flasher_flash_embedded_uart(uart_port_t /*uart*/, gpio_num_t /*tx_pin*/, gpio_num_t /*rx_pin*/,
                                            uint32_t /*baud_rate*/, bool /*allow_overwrite*/,
                                            sidekick_flasher_info_t* out_info,
                                            char* /*out_diag*/, size_t /*out_diag_size*/) {
    if (out_info) {
        *out_info = {};
        out_info->status = SIDEKICK_FLASHER_STATUS_UNKNOWN;
        out_info->chip_id = -1;
    }
    ESP_LOGE(TAG, "No Nano firmware staged — run tools/stage_sidekick_firmware.sh before building");
    return ESP_ERR_NOT_FOUND;
}

bool sidekick_flasher_embedded_version(char* /*out*/, size_t /*out_size*/) {
    return false;
}

#endif
