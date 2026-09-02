#pragma once

#include <cstddef>
#include <cstdint>

enum class UsbCKind : uint8_t {
    QmxQdx = 0,
    GreenNano,
    Other,
};

enum class UsbCPresenceAction : uint8_t {
    Attach = 0,
    Detach,
};

struct UsbCDevice {
    UsbCKind kind;
    uint16_t vid;
    uint16_t pid;
};

struct UsbCPresenceEvent {
    UsbCPresenceAction action;
    UsbCDevice device;
};

static constexpr uint16_t kUsbCQmxVid = 0x0483;
static constexpr uint16_t kUsbCQmxPid = 0xA34C;
static constexpr uint16_t kUsbCEspressifVid = 0x303A;

UsbCKind usb_c_classify(uint16_t vid, uint16_t pid);
void usb_c_format_attach(const UsbCDevice& dev, char* title, size_t title_n, char* body, size_t body_n);
void usb_c_format_detach(const UsbCDevice& last, char* title, size_t title_n, char* body, size_t body_n);

#ifndef HOST_MOCK
#include "esp_err.h"

esp_err_t usb_c_presence_start(void);
void usb_c_presence_stop(void);
bool usb_c_presence_take_event(UsbCPresenceEvent* out);
// Drop attach/detach banners (S→2 re-probe). Resets the event queue.
void usb_c_presence_set_notify(bool enabled);
// Close an open presence handle so UAC/CDC can claim the device.
void usb_c_presence_yield_device(void);

typedef void (*usb_c_uac_iface_cb)(uint8_t addr, uint8_t iface, bool is_rx, void* arg);
// Walk devices already on the bus (no power cycle). For each UAC streaming iface.
esp_err_t usb_c_presence_probe_uac_interfaces(usb_c_uac_iface_cb cb, void* arg);
#endif
