#pragma once

#include <cstddef>

// Band Config list: per-band enable plus frequency edit. Enabled bands are the
// STATUS band-step rotation (the old ActiveBand text). No ESP-IDF; host tests
// and firmware both link this.

enum class BandConfigEvent {
    None = 0,
    Toggle,
    EditFrequency,
    PageChanged,
    Exit,
};

void band_config_reset(int band_count);
int band_config_page(void);
int band_config_focus(void);

BandConfigEvent band_config_handle_key(char key);

// Enable set. The last enabled band cannot be turned off, so the rotation is never empty.
int band_config_enabled_count(const bool* enabled, int count);
bool band_config_toggle(bool* enabled, int count, int index);

// `text` is the saved active-band list ("80 40 20"); `numbers` is the band number
// per row (80, 40, ...). Empty or unmatched text enables every band.
void band_config_from_text(const char* text, const int* numbers, int count, bool* enabled);
int band_config_to_text(const bool* enabled, const int* numbers, int count, char* out,
                        std::size_t out_len);
