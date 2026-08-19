#pragma once

// Decode-list order: to_me, then fresh CQ (strongest first), then other fresh,
// then recent QSOs. No ESP-IDF. Host tests and firmware both use this.

struct DecodeSortEntry {
    bool is_to_me;
    bool is_recent_qso;
    bool is_cq;
    int snr;
};

static inline int decode_sort_group(const DecodeSortEntry* d)
{
    if (d->is_to_me) {
        return 0;
    }
    if (d->is_recent_qso) {
        return 3;
    }
    if (d->is_cq) {
        return 1;
    }
    return 2;
}

static inline int decode_sort_cmp(const DecodeSortEntry* a, const DecodeSortEntry* b)
{
    const int ga = decode_sort_group(a);
    const int gb = decode_sort_group(b);
    if (ga != gb) {
        return ga - gb;
    }
    if (ga == 1 && a->snr != b->snr) {
        return b->snr - a->snr;
    }
    return 0;
}
