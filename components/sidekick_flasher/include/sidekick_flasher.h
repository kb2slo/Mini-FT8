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
// False until `tools/stage_sidekick_firmware.sh` has copied a `sidekick`
// build into components/sidekick_flasher/target_firmware/ before this app was
// configured. See RFC 0001 §5.1.
bool sidekick_flasher_has_firmware(void);

// Copies the embedded sidekick build's own version string (up to 32 bytes
// plus NUL) into `out` — the same value sidekick_flasher_flash_embedded()
// compares a connected Nano against, and what a PORTA companion beacon
// (RFC 0001 §5.2c) should be compared against too, without needing a USB-C
// session. False if no firmware is embedded (sidekick_flasher_has_firmware()
// false) or `out_size` is too small; `out` is left empty either way.
bool sidekick_flasher_embedded_version(char* out, size_t out_size);

typedef enum {
    SIDEKICK_FLASHER_STATUS_UNKNOWN = 0,    // connect/read itself failed
    SIDEKICK_FLASHER_STATUS_NOT_INSTALLED,  // esp_app_desc project_name != "sidekick"
    SIDEKICK_FLASHER_STATUS_UP_TO_DATE,     // project_name + version both match embedded build
    SIDEKICK_FLASHER_STATUS_UPDATED,        // was NOT_INSTALLED or a different version; now flashed
} sidekick_flasher_status_t;

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
esp_err_t sidekick_flasher_flash_embedded(uint16_t vid, uint16_t pid,
                                       sidekick_flasher_status_t* out_status,
                                       char* out_remote_version, size_t out_remote_version_size);

// PORTA UART variant (RFC 0001 §5.2c "Update flash over PORTA") — same
// read-then-decide/write logic and out_status/out_remote_version contract
// as sidekick_flasher_flash_embedded(), only the transport differs. Caller must
// have already stopped porta.cpp's normal arbitration (porta_stop()) so
// this owns UART1 exclusively — which means disconnecting or silencing a
// beaconing sidekick first, since it shares that bus.
//
// Unlike the USB-C path, this cannot reset the target into download mode
// itself — PORTA carries only TX/RX/5V/GND, no reset or boot control. The
// target must already be sitting in its ROM bootloader when this is called.
// This function does not wait for that beyond esp_loader_connect()'s own
// retry window (~15s) — call it after prompting the operator, and be
// prepared to call it again if that window elapses first.
//
// How the target gets there depends on the chip, and this is the reason the
// function has never had a caller:
//
//   NanoC6 (the original target): it could not. The plan was a sustained
//   hold of the on-board GPIO9 button triggering a self-restart, landing in
//   download mode because the finger is holding GPIO9 low at that reset.
//   Bench-tested 2026-09-04 and it does not work — a software (SW_CPU)
//   reset does not make the ROM re-sample GPIO9 at all. That left this
//   transport built but unreachable, and USB-C the only flash path.
//
//   AtomS3 Lite (the target since I3's retarget): the button-hold problem
//   does not carry over — S3 straps on GPIO0, and a hardware reset with the
//   on-board button held does re-sample them. But that no longer matters,
//   because of a deeper blocker found 2026-09-09 while planning that very
//   experiment:
//
// THE ROM BOOTLOADER DOES NOT LISTEN ON THE GROVE PINS. It runs before any
// GPIO-matrix remapping exists, so it only accepts a download session on
// UART0's default pads: GPIO43/44 on S3, GPIO16/17 on C6 (soc/uart_pins.h).
// PORTA/Grove is GPIO1/GPIO2 on both parts. So no amount of correct
// download-mode entry helps — the two ends would be on different pins.
//
// This function therefore cannot flash a stock target over Grove on either
// chip, and never could; the C6 download-mode failure masked it. It is kept
// because the transport itself is sound and is exactly what a custom
// second-stage bootloader would use: code running from flash CAN take any
// pins through the GPIO matrix, which is precisely the capability the ROM
// lacks. RFC 0001 §5.2's parked "future phase" is not one option among
// several — it is the only path to PORTA updates.
// Field-only — real hardware required, cannot be host-tested.
// out_diag (nullable; keep short, ~16 chars, if the caller has a display-
// row budget) reports which stage failed — PORTA has no serial console
// fallback, so unlike a bring-up aid this is a lasting diagnostic.
esp_err_t sidekick_flasher_flash_embedded_uart(uart_port_t uart, gpio_num_t tx_pin, gpio_num_t rx_pin,
                                            uint32_t baud_rate,
                                            sidekick_flasher_status_t* out_status,
                                            char* out_remote_version, size_t out_remote_version_size,
                                            char* out_diag, size_t out_diag_size);

#ifdef __cplusplus
}
#endif
