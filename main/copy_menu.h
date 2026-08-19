#pragma once

#include "copy_block.h"

#include <cstdint>
#include <string>

static constexpr int kCopyMenuLineAbs = 16;
static constexpr std::int64_t kCopyMenuFeedbackMs = 1800;
static constexpr std::int64_t kCopyMenuFlashMs = 500;

struct CopyMenuState {
    std::string feedback;
    std::int64_t feedback_until_ms = 0;
    std::int64_t flash_until_ms = 0;
    bool dialog_active = false;
};

static inline const char* copy_menu_block_message(CopyBlockReason why)
{
    switch (why) {
        case CopyBlockReason::None:
            return nullptr;
        case CopyBlockReason::RadioBusy:
            return "Radio busy";
        case CopyBlockReason::StorageBusy:
            return "Storage busy";
    }
    return nullptr;
}

static inline void copy_menu_set_message(CopyMenuState* s, const char* text, std::int64_t now_ms)
{
    s->feedback = (text != nullptr) ? text : "";
    if (s->feedback.size() > 19) {
        s->feedback.resize(19);
    }
    s->feedback_until_ms = now_ms + kCopyMenuFeedbackMs;
    s->flash_until_ms = now_ms + kCopyMenuFlashMs;
    s->dialog_active = false;
}

static inline void copy_menu_clear_if_expired(CopyMenuState* s, std::int64_t now_ms)
{
    if (s->feedback_until_ms > 0 && now_ms >= s->feedback_until_ms) {
        s->feedback_until_ms = 0;
        s->feedback.clear();
    }
}

static inline const char* copy_menu_item_text(CopyMenuState* s, std::int64_t now_ms)
{
    copy_menu_clear_if_expired(s, now_ms);
    if (!s->feedback.empty()) {
        return s->feedback.c_str();
    }
    return "Copy Files to SD";
}

static inline int copy_menu_flash_abs(const CopyMenuState* s, std::int64_t now_ms)
{
    if (s->flash_until_ms > 0 && now_ms < s->flash_until_ms) {
        return kCopyMenuLineAbs;
    }
    return -1;
}
