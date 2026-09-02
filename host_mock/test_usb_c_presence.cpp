#include <cstdio>
#include <cstring>

#include "usb_c_presence.h"

static int g_fails = 0;

static void expect_kind(UsbCKind got, UsbCKind want, const char* msg)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %u want %u)\n", msg, static_cast<unsigned>(got),
                static_cast<unsigned>(want));
        ++g_fails;
    }
}

static void expect_str(const char* got, const char* want, const char* msg)
{
    if (std::strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL: %s (got '%s' want '%s')\n", msg, got, want);
        ++g_fails;
    }
}

int main()
{
    expect_kind(usb_c_classify(0x0483, 0xA34C), UsbCKind::QmxQdx, "QMX/QDX");
    expect_kind(usb_c_classify(0x303A, 0x1001), UsbCKind::GreenNano, "Nano JTAG");
    expect_kind(usb_c_classify(0x303A, 0x4001), UsbCKind::GreenNano, "Nano other PID");
    expect_kind(usb_c_classify(0x1234, 0x5678), UsbCKind::Other, "unknown");
    expect_kind(usb_c_classify(0x0483, 0x0001), UsbCKind::Other, "STM32 not QMX");

    char title[24];
    char body[32];
    UsbCDevice qmx = {UsbCKind::QmxQdx, 0x0483, 0xA34C};
    usb_c_format_attach(qmx, title, sizeof(title), body, sizeof(body));
    expect_str(title, "QMX/QDX", "qmx title");
    expect_str(body, "USB radio", "qmx body");
    usb_c_format_detach(qmx, title, sizeof(title), body, sizeof(body));
    expect_str(title, "USB unplugged", "detach title");
    expect_str(body, "QMX/QDX", "detach qmx");

    UsbCDevice nano = {UsbCKind::GreenNano, 0x303A, 0x1001};
    usb_c_format_attach(nano, title, sizeof(title), body, sizeof(body));
    expect_str(title, "Green Nano", "nano title");
    expect_str(body, "Flash Nano USB-C", "nano body");

    UsbCDevice other = {UsbCKind::Other, 0x1234, 0xABCD};
    usb_c_format_attach(other, title, sizeof(title), body, sizeof(body));
    expect_str(title, "USB device", "other title");
    expect_str(body, "VID 1234 PID ABCD", "other body");
    usb_c_format_detach(other, title, sizeof(title), body, sizeof(body));
    expect_str(body, "VID 1234 PID ABCD", "detach other");

    if (g_fails != 0) {
        fprintf(stderr, "%d FAIL\n", g_fails);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
