#include <cstdio>
#include <cstring>
#include <string>

#include "copy_block.h"
#include "copy_menu.h"

static int g_fails = 0;

static void expect_reason(CopyBlockReason got, CopyBlockReason want, const char* msg)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %d want %d)\n", msg, (int)got, (int)want);
        ++g_fails;
    }
}

int main()
{
    CopyBlockInputs in;

    expect_reason(copy_block_reason(in), CopyBlockReason::None, "idle");

    in = {};
    in.audio_streaming = true;
    expect_reason(copy_block_reason(in), CopyBlockReason::RadioBusy, "uac");

    in = {};
    in.decode_active = true;
    expect_reason(copy_block_reason(in), CopyBlockReason::RadioBusy, "decode");

    in = {};
    in.tx_active = true;
    expect_reason(copy_block_reason(in), CopyBlockReason::RadioBusy, "tx");

    in = {};
    in.audio_streaming = true;
    in.open_streams = 1;
    expect_reason(copy_block_reason(in), CopyBlockReason::RadioBusy, "radio wins over stream");

    in = {};
    in.open_streams = 1;
    expect_reason(copy_block_reason(in), CopyBlockReason::StorageBusy, "open stream");

    in = {};
    in.usb_drive = true;
    expect_reason(copy_block_reason(in), CopyBlockReason::StorageBusy, "usb drive");

    in = {};
    in.firmware_owns = false;
    expect_reason(copy_block_reason(in), CopyBlockReason::StorageBusy, "not owner");

    in = {};
    in.writes_blocked = true;
    expect_reason(copy_block_reason(in), CopyBlockReason::StorageBusy, "low batt writes");

    in = {};
    in.host_bin_active = true;
    expect_reason(copy_block_reason(in), CopyBlockReason::StorageBusy, "host bin");

    in = {};
    in.ui_msc = true;
    expect_reason(copy_block_reason(in), CopyBlockReason::StorageBusy, "msc ui");

    {
        CopyMenuState s;
        if (copy_menu_block_message(CopyBlockReason::None) != nullptr) {
            fprintf(stderr, "FAIL: none has no block text\n");
            ++g_fails;
        }
        if (std::strcmp(copy_menu_block_message(CopyBlockReason::RadioBusy), "Radio busy") != 0) {
            fprintf(stderr, "FAIL: radio busy text\n");
            ++g_fails;
        }
        if (std::strcmp(copy_menu_block_message(CopyBlockReason::StorageBusy), "Storage busy") != 0) {
            fprintf(stderr, "FAIL: storage busy text\n");
            ++g_fails;
        }
        copy_menu_set_message(&s, "Radio busy", 1000);
        if (std::strcmp(copy_menu_item_text(&s, 1000), "Radio busy") != 0) {
            fprintf(stderr, "FAIL: item while flashing\n");
            ++g_fails;
        }
        if (copy_menu_flash_abs(&s, 1000) != kCopyMenuLineAbs) {
            fprintf(stderr, "FAIL: flash abs\n");
            ++g_fails;
        }
        if (copy_menu_flash_abs(&s, 1000 + kCopyMenuFlashMs) != -1) {
            fprintf(stderr, "FAIL: flash expired\n");
            ++g_fails;
        }
        if (std::strcmp(copy_menu_item_text(&s, 1000 + kCopyMenuFlashMs), "Radio busy") != 0) {
            fprintf(stderr, "FAIL: item after flash still feedback\n");
            ++g_fails;
        }
        if (std::strcmp(copy_menu_item_text(&s, 1000 + kCopyMenuFeedbackMs), "Copy Files to SD") !=
            0) {
            fprintf(stderr, "FAIL: item after feedback\n");
            ++g_fails;
        }
        copy_menu_set_message(&s, "this message is way too long for the menu", 0);
        if (s.feedback.size() != 19) {
            fprintf(stderr, "FAIL: truncate to 19 (got %zu)\n", s.feedback.size());
            ++g_fails;
        }
    }

    if (g_fails != 0) {
        fprintf(stderr, "%d failures\n", g_fails);
        return 1;
    }
    printf("PASS: copy block and menu\n");
    return 0;
}
