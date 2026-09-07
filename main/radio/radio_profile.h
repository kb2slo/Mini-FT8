#pragma once

#include "audio_source.h"
#include "radio_control.h"
#include "station_types.h"

// Product radio picker. Add a radio by appending a row (and a radio_control
// ops file). MENU cycle, Station radio= integers, and audio/CAT bind read
// this table. No ESP-IDF. Host tests share it.
//
// I21 (2026-09-04): dropped KH1-USBC/KH1-MIC — untestable without hardware
// (none owned) and the only PORTA/UART1 sharer, which no longer applies now
// that every remaining radio uses USB UAC exclusively. Open to reintroducing
// KH1 later if someone picks it up with real hardware to verify against.

struct RadioProfile {
    RadioType type;
    const char* name;
    audio_source_backend_t audio_backend;
    radio_control_backend_t radio_backend;
    bool audio_is_uac;
};

inline const RadioProfile kRadioProfiles[] = {
    {RadioType::QMX, "QMX", AUDIO_SOURCE_QMX_UAC, RADIO_CONTROL_QMX, true},
    {RadioType::QDX, "QDX", AUDIO_SOURCE_QMX_UAC, RADIO_CONTROL_QDX, true},
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
