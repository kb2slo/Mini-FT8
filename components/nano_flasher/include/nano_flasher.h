#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// True when this build has embedded sidekick firmware to flash.
// False until `tools/stage_nano_firmware.sh` has copied a `sidekick`
// build into components/nano_flasher/target_firmware/ before this app was
// configured. See RFC 0001 §5.1.
bool nano_flasher_has_firmware(void);

// Copies the embedded sidekick build's own version string (up to 32 bytes
// plus NUL) into `out` — the same value nano_flasher_flash_embedded()
// compares a connected Nano against, and what a PORTA companion beacon
// (RFC 0001 §5.2c) should be compared against too, without needing a USB-C
// session. False if no firmware is embedded (nano_flasher_has_firmware()
// false) or `out_size` is too small; `out` is left empty either way.
bool nano_flasher_embedded_version(char* out, size_t out_size);

typedef enum {
    NANO_FLASHER_STATUS_UNKNOWN = 0,    // connect/read itself failed
    NANO_FLASHER_STATUS_NOT_INSTALLED,  // esp_app_desc project_name != "sidekick"
    NANO_FLASHER_STATUS_UP_TO_DATE,     // project_name + version both match embedded build
    NANO_FLASHER_STATUS_UPDATED,        // was NOT_INSTALLED or a different version; now flashed
} nano_flasher_status_t;

// Connects once (ROM bootloader over USB-C — this resets whatever's
// currently running on the Nano, same as any esp_loader session), checks
// the ROM-reported chip family is actually ESP32-C6 (refuses to write
// otherwise — VID/PID-based presence detection is a heuristic and can
// false-positive on another Espressif-VID device), then reads the
// esp_app_desc_t off its flash and compares project_name/version against
// the embedded sidekick build (RFC 0001 §5.2b) before deciding whether to
// write anything:
//   - chip family isn't ESP32-C6             -> refuse (UNKNOWN, no write)
//   - not recognized as "sidekick"          -> flash (NOT_INSTALLED path)
//   - recognized, version differs           -> flash (update path)
//   - recognized, version matches           -> skip the write entirely
// out_status and out_remote_version (if non-NULL; pass a buffer of at least
// 33 bytes) report what was found, even on the skip-write path.
// Caller must have already parked any other USB host client
// (usb_c_presence_yield_device()) and must not touch the USB host from
// another task while this runs. Blocking; runs on the calling task.
// Field-only — real hardware required, cannot be host-tested.
esp_err_t nano_flasher_flash_embedded(uint16_t vid, uint16_t pid,
                                       nano_flasher_status_t* out_status,
                                       char* out_remote_version, size_t out_remote_version_size);

// PORTA UART variant (RFC 0001 §5.2c "Update flash over PORTA") — same
// read-then-decide/write logic and out_status/out_remote_version contract
// as nano_flasher_flash_embedded(), only the transport differs. Caller must
// have already stopped porta.cpp's normal arbitration (porta_stop()) so
// this owns UART1 exclusively.
//
// Unlike the USB-C path, this cannot reset the Nano into download mode
// itself — PORTA has no wired reset/boot control, and ESP32-C6 has no
// software-only path into download mode (checked against Espressif's own
// esptool docs). sidekick must already be sitting in its ROM bootloader by
// the time this is called: it self-restarts on a sustained hold of its
// on-board GPIO9 button, landing in download mode because the operator's
// finger is physically holding GPIO9 low at that exact reset. This
// function does not wait for that to happen on its own initiative beyond
// esp_loader_connect()'s own generous retry window (~15s) — call it after
// prompting the operator to hold the button, and be prepared to call it
// again if that window elapses before they do.
// Field-only — real hardware required, cannot be host-tested.
// out_diag (nullable; keep short, ~16 chars, if the caller has a display-
// row budget) reports which stage failed — PORTA has no serial console
// fallback, so unlike a bring-up aid this is a lasting diagnostic.
esp_err_t nano_flasher_flash_embedded_uart(uart_port_t uart, gpio_num_t tx_pin, gpio_num_t rx_pin,
                                            uint32_t baud_rate,
                                            nano_flasher_status_t* out_status,
                                            char* out_remote_version, size_t out_remote_version_size,
                                            char* out_diag, size_t out_diag_size);

#ifdef __cplusplus
}
#endif
