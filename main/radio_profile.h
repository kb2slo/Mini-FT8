#pragma once

#include "audio_source.h"
#include "radio_control.h"
#include "station_types.h"

// Product radio picker. Add a radio by appending a row (and a radio_control
// ops file). MENU cycle, Station radio= integers, audio/CAT bind, and PORTA
// UART vs GPS policy read this table. No ESP-IDF. Host tests share it.

struct RadioProfile {
    RadioType type;
    const char* name;
    audio_source_backend_t audio_backend;
    radio_control_backend_t radio_backend;
    bool audio_is_uac;
    bool shares_porta_uart;
    bool needs_manual_connect;
    bool defer_cat_on_audio_start;
};

inline const RadioProfile kRadioProfiles[] = {
    {RadioType::QMX, "QMX", AUDIO_SOURCE_QMX_UAC, RADIO_CONTROL_QMX, true, false, false, false},
    {RadioType::QDX, "QDX", AUDIO_SOURCE_QMX_UAC, RADIO_CONTROL_QDX, true, false, false, false},
    {RadioType::KH1_USBC, "KH1-USBC", AUDIO_SOURCE_USB_UAC_GENERIC, RADIO_CONTROL_KH1_CAT,
     true, true, true, true},
    {RadioType::KH1_MIC, "KH1-MIC", AUDIO_SOURCE_KH1_MIC, RADIO_CONTROL_KH1_CAT,
     false, true, true, false},
};

inline constexpr int kRadioProfileCount =
    (int)(sizeof(kRadioProfiles) / sizeof(kRadioProfiles[0]));

inline const RadioProfile* radio_profile_find(RadioType type)
{
    for (int i = 0; i < kRadioProfileCount; ++i) {
        if (kRadioProfiles[i].type == type) {
            return &kRadioProfiles[i];
        }
    }
    return nullptr;
}

inline const RadioProfile& radio_profile_get(RadioType type)
{
    const RadioProfile* found = radio_profile_find(type);
    if (found) {
        return *found;
    }
    return kRadioProfiles[0];
}

inline RadioType radio_profile_canonical(RadioType type)
{
    return radio_profile_get(type).type;
}

inline RadioType radio_profile_from_saved_int(int value)
{
    for (int i = 0; i < kRadioProfileCount; ++i) {
        if ((int)kRadioProfiles[i].type == value) {
            return kRadioProfiles[i].type;
        }
    }
    return RadioType::QMX;
}

inline RadioType radio_profile_next(RadioType type)
{
    const RadioType cur = radio_profile_canonical(type);
    int idx = 0;
    for (int i = 0; i < kRadioProfileCount; ++i) {
        if (kRadioProfiles[i].type == cur) {
            idx = i;
            break;
        }
    }
    return kRadioProfiles[(idx + 1) % kRadioProfileCount].type;
}

inline const char* radio_profile_name(RadioType type)
{
    return radio_profile_get(type).name;
}

inline bool radio_profile_audio_is_uac(RadioType type)
{
    return radio_profile_get(type).audio_is_uac;
}

inline bool radio_profile_shares_porta_uart(RadioType type)
{
    return radio_profile_get(type).shares_porta_uart;
}

inline bool radio_profile_needs_manual_connect(RadioType type)
{
    return radio_profile_get(type).needs_manual_connect;
}

inline bool radio_profile_defer_cat_on_audio_start(RadioType type)
{
    return radio_profile_get(type).defer_cat_on_audio_start;
}

inline bool radio_profile_porta_gps_should_run(RadioType type, bool cat_connected, bool gnss_lora)
{
    if (!radio_profile_shares_porta_uart(type)) {
        return true;
    }
    if (!cat_connected) {
        return true;
    }
    return gnss_lora;
}
