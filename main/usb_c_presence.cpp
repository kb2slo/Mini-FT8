#include <cstdio>

#include "usb_c_presence.h"

UsbCKind usb_c_classify(uint16_t vid, uint16_t pid)
{
    if (vid == kUsbCQmxVid && pid == kUsbCQmxPid) {
        return UsbCKind::QmxQdx;
    }
    if (vid == kUsbCEspressifVid && pid == kUsbCNanoJtagPid) {
        return UsbCKind::GreenNano;
    }
    return UsbCKind::Other;
}

static void format_kind_or_ids(const UsbCDevice& dev, char* dest, size_t dest_n)
{
    if (!dest || dest_n == 0) {
        return;
    }
    switch (dev.kind) {
    case UsbCKind::QmxQdx:
        std::snprintf(dest, dest_n, "QMX/QDX");
        break;
    case UsbCKind::GreenNano:
        std::snprintf(dest, dest_n, "Green Nano");
        break;
    case UsbCKind::Other:
        std::snprintf(dest, dest_n, "VID %04X PID %04X", dev.vid, dev.pid);
        break;
    }
}

void usb_c_format_attach(const UsbCDevice& dev, char* title, size_t title_n, char* body, size_t body_n)
{
    if (title && title_n > 0) {
        title[0] = 0;
    }
    if (body && body_n > 0) {
        body[0] = 0;
    }
    switch (dev.kind) {
    case UsbCKind::QmxQdx:
        if (title && title_n > 0) {
            std::snprintf(title, title_n, "QMX/QDX");
        }
        if (body && body_n > 0) {
            std::snprintf(body, body_n, "USB radio");
        }
        break;
    case UsbCKind::GreenNano:
        if (title && title_n > 0) {
            std::snprintf(title, title_n, "Green Nano");
        }
        if (body && body_n > 0) {
            std::snprintf(body, body_n, "Flash Nano USB-C");
        }
        break;
    case UsbCKind::Other:
        if (title && title_n > 0) {
            std::snprintf(title, title_n, "USB device");
        }
        if (body && body_n > 0) {
            std::snprintf(body, body_n, "VID %04X PID %04X", dev.vid, dev.pid);
        }
        break;
    }
}

void usb_c_format_detach(const UsbCDevice& last, char* title, size_t title_n, char* body, size_t body_n)
{
    if (title && title_n > 0) {
        std::snprintf(title, title_n, "USB unplugged");
    }
    format_kind_or_ids(last, body, body_n);
}
