#include "menu_model.h"

#include <cctype>
#include <cstring>

// On-screen order. Page and key fall out of the index: row i is on page
// i / kMenuRowsPerPage and answers key '1' + (i % kMenuRowsPerPage).
const MenuRow kMenuRows[] = {
    // page 0
    { "cq_type",     MenuEdit::None     },
    { "send_ft",     MenuEdit::None     },
    { "freetext",    MenuEdit::None     },
    { "call",        MenuEdit::Callsign },
    { "grid",        MenuEdit::Callsign },
    { "sleep_batt",  MenuEdit::None     },
    // page 1
    { "offset_src",  MenuEdit::None     },
    { "offset_hz",   MenuEdit::Numeric  },
    { "radio",       MenuEdit::None     },
    { "ignore_list", MenuEdit::None     },
    { "comment",     MenuEdit::None     },
    { "protocol",    MenuEdit::None     },
    // page 2
    { "rxtx_log",    MenuEdit::None     },
    { "skip_tx1",    MenuEdit::None     },
    { "band_config", MenuEdit::None     },
    { "gnss_lora",   MenuEdit::None     },
    { "copy_to_sd",  MenuEdit::None     },
    { "max_retry",   MenuEdit::Numeric  },
};

static constexpr int kRowCount = (int)(sizeof(kMenuRows) / sizeof(kMenuRows[0]));

int menu_row_count(void) { return kRowCount; }

int menu_page_count(void) {
    return (kRowCount + kMenuRowsPerPage - 1) / kMenuRowsPerPage;
}

static bool in_range(int idx) { return idx >= 0 && idx < kRowCount; }

int menu_page_of(int idx) {
    if (!in_range(idx)) return -1;
    return idx / kMenuRowsPerPage;
}

char menu_key_of(int idx) {
    if (!in_range(idx)) return 0;
    return (char)('1' + (idx % kMenuRowsPerPage));
}

int menu_index_for(int page, char key) {
    if (page < 0 || page >= menu_page_count()) return -1;
    if (key < '1' || key >= '1' + kMenuRowsPerPage) return -1;
    const int idx = page * kMenuRowsPerPage + (key - '1');
    return in_range(idx) ? idx : -1;
}

MenuEdit menu_edit_class(int idx) {
    return in_range(idx) ? kMenuRows[idx].edit : MenuEdit::None;
}

const char* menu_row_id(int idx) {
    return in_range(idx) ? kMenuRows[idx].id : "";
}

bool menu_edit_accepts(MenuEdit kind, char c, size_t current_len, char* out_ch) {
    if (c < 32 || c >= 127) return false;   // non-printable never appends
    switch (kind) {
    case MenuEdit::Numeric:
        if (c < '0' || c > '9') return false;
        if (current_len >= kMenuNumericMaxLen) return false;
        if (out_ch) *out_ch = c;
        return true;
    case MenuEdit::Callsign:
        if (out_ch) *out_ch = (char)std::toupper((unsigned char)c);
        return true;
    case MenuEdit::None:
    default:
        return false;
    }
}
