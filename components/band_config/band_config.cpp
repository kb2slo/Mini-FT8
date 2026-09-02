#include "band_config.h"

#include <cstdio>
#include <cstring>

static constexpr int kRowsPerPage = 6;

static int s_count = 0;
static int s_page = 0;
static int s_focus = 0;

static int last_page(void)
{
    if (s_count <= 0) {
        return 0;
    }
    return (s_count - 1) / kRowsPerPage;
}

static int clamp_page(int page)
{
    if (page < 0) {
        return 0;
    }
    const int max_page = last_page();
    if (page > max_page) {
        return max_page;
    }
    return page;
}

void band_config_reset(int band_count)
{
    s_count = band_count < 0 ? 0 : band_count;
    s_page = 0;
    s_focus = 0;
}

int band_config_page(void)
{
    return s_page;
}

int band_config_focus(void)
{
    return s_focus;
}

BandConfigEvent band_config_handle_key(char key)
{
    switch (key) {
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6': {
            const int index = s_page * kRowsPerPage + (key - '1');
            if (index < 0 || index >= s_count) {
                return BandConfigEvent::None;
            }
            s_focus = index;
            return BandConfigEvent::Toggle;
        }
        case ';': {
            const int next = clamp_page(s_page - 1);
            if (next == s_page) {
                return BandConfigEvent::None;
            }
            s_page = next;
            return BandConfigEvent::PageChanged;
        }
        case '.': {
            const int next = clamp_page(s_page + 1);
            if (next == s_page) {
                return BandConfigEvent::None;
            }
            s_page = next;
            return BandConfigEvent::PageChanged;
        }
        case '\r':
        case '\n':
            if (s_focus < 0 || s_focus >= s_count) {
                return BandConfigEvent::None;
            }
            return BandConfigEvent::EditFrequency;
        case '`':
            return BandConfigEvent::Exit;
        default:
            return BandConfigEvent::None;
    }
}

int band_config_enabled_count(const bool* enabled, int count)
{
    if (enabled == nullptr || count <= 0) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < count; ++i) {
        if (enabled[i]) {
            ++n;
        }
    }
    return n;
}

bool band_config_toggle(bool* enabled, int count, int index)
{
    if (enabled == nullptr || index < 0 || index >= count) {
        return false;
    }
    if (enabled[index] && band_config_enabled_count(enabled, count) <= 1) {
        return false;
    }
    enabled[index] = !enabled[index];
    return true;
}

void band_config_from_text(const char* text, const int* numbers, int count, bool* enabled)
{
    if (enabled == nullptr || count <= 0) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        enabled[i] = false;
    }
    int matched = 0;
    if (text != nullptr && numbers != nullptr) {
        const char* p = text;
        while (*p != '\0') {
            while (*p != '\0' && (*p < '0' || *p > '9')) {
                ++p;
            }
            if (*p == '\0') {
                break;
            }
            int value = 0;
            while (*p >= '0' && *p <= '9') {
                value = value * 10 + (*p - '0');
                ++p;
            }
            if (value <= 0) {
                continue;
            }
            for (int i = 0; i < count; ++i) {
                if (numbers[i] == value && !enabled[i]) {
                    enabled[i] = true;
                    ++matched;
                    break;
                }
            }
        }
    }
    if (matched == 0) {
        for (int i = 0; i < count; ++i) {
            enabled[i] = true;
        }
    }
}

int band_config_to_text(const bool* enabled, const int* numbers, int count, char* out,
                        std::size_t out_len)
{
    if (out == nullptr || out_len == 0) {
        return 0;
    }
    out[0] = '\0';
    if (enabled == nullptr || numbers == nullptr || count <= 0) {
        return 0;
    }
    std::size_t used = 0;
    int wrote = 0;
    for (int i = 0; i < count; ++i) {
        if (!enabled[i]) {
            continue;
        }
        char token[16];
        const int n = std::snprintf(token, sizeof(token), "%s%d", wrote == 0 ? "" : " ", numbers[i]);
        if (n <= 0 || used + static_cast<std::size_t>(n) + 1 > out_len) {
            break;
        }
        std::memcpy(out + used, token, static_cast<std::size_t>(n));
        used += static_cast<std::size_t>(n);
        out[used] = '\0';
        ++wrote;
    }
    return wrote;
}
