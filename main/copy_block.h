#pragma once

#include <cstddef>

// Why Copy Files to SD must not start. No ESP-IDF. RadioBusy is TX, decode, or
// live USB audio — fail and tell the operator; do not wait it out.

enum class CopyBlockReason {
    None,
    RadioBusy,
    StorageBusy,
};

struct CopyBlockInputs {
    bool writes_blocked = false;
    bool tx_active = false;
    bool decode_active = false;
    bool audio_streaming = false;
    bool host_bin_active = false;
    bool ui_msc = false;
    bool usb_drive = false;
    bool firmware_owns = true;
    size_t open_streams = 0;
};

static inline CopyBlockReason copy_block_reason(const CopyBlockInputs& in)
{
    if (in.tx_active || in.decode_active || in.audio_streaming) {
        return CopyBlockReason::RadioBusy;
    }
    if (in.writes_blocked || in.host_bin_active || in.ui_msc || in.usb_drive ||
        !in.firmware_owns || in.open_streams != 0) {
        return CopyBlockReason::StorageBusy;
    }
    return CopyBlockReason::None;
}
