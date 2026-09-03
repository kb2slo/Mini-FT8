#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// True when this build has embedded Nano companion firmware to flash.
// False until `tools/stage_nano_firmware.sh` has copied a `nano_companion`
// build into components/nano_flasher/target_firmware/ before this app was
// configured. See RFC 0001 §5.1.
bool nano_flasher_has_firmware(void);

// Flash the embedded bootloader + partition table + app onto a Nano already
// enumerated as a USB CDC-ACM device on the ADV's USB-C host (Green Nano,
// B18 presence VID 0x303A). Caller must have already parked any other USB
// host client (usb_c_presence_yield_device()) and must not touch the USB
// host from another task while this runs. Blocking; runs on the calling
// task. Field-only — real hardware required, cannot be host-tested.
esp_err_t nano_flasher_flash_embedded(uint16_t vid, uint16_t pid);

#ifdef __cplusplus
}
#endif
