#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "decode_sort.h"

static int g_fails = 0;

static void fail(const char* msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    ++g_fails;
}

static void expect_true(bool cond, const char* msg)
{
    if (!cond) {
        fail(msg);
    }
}

static void expect_int(int got, int want, const char* msg)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %d want %d)\n", msg, got, want);
        ++g_fails;
    }
}

static DecodeSortEntry entry(bool to_me, bool recent, bool cq, int snr)
{
    DecodeSortEntry e;
    e.is_to_me = to_me;
    e.is_recent_qso = recent;
    e.is_cq = cq;
    e.snr = snr;
    return e;
}

static int cmp_qsort(const void* a, const void* b)
{
    return decode_sort_cmp(static_cast<const DecodeSortEntry*>(a),
                           static_cast<const DecodeSortEntry*>(b));
}

int main()
{
    const DecodeSortEntry to_me = entry(true, false, false, -10);
    const DecodeSortEntry cq_weak = entry(false, false, true, -18);
    const DecodeSortEntry cq_strong = entry(false, false, true, -5);
    const DecodeSortEntry other = entry(false, false, false, -2);
    const DecodeSortEntry recent = entry(false, true, false, 12);
    const DecodeSortEntry to_me_recent = entry(true, true, false, 0);

    expect_int(decode_sort_group(&to_me), 0, "to_me group");
    expect_int(decode_sort_group(&cq_weak), 1, "cq group");
    expect_int(decode_sort_group(&other), 2, "other group");
    expect_int(decode_sort_group(&recent), 3, "recent group");
    expect_int(decode_sort_group(&to_me_recent), 0, "to_me beats recent flag");

    expect_true(decode_sort_cmp(&to_me, &cq_strong) < 0, "to_me before cq");
    expect_true(decode_sort_cmp(&cq_strong, &other) < 0, "cq before other");
    expect_true(decode_sort_cmp(&other, &recent) < 0, "other before recent");
    expect_true(decode_sort_cmp(&cq_strong, &cq_weak) < 0, "stronger cq first");
    const DecodeSortEntry other_loud = entry(false, false, false, 20);
    expect_int(decode_sort_cmp(&other, &other_loud), 0,
               "non-cq snr is not a tiebreak");

    DecodeSortEntry rows[] = {recent, cq_weak, other, to_me, cq_strong};
    qsort(rows, 5, sizeof(rows[0]), cmp_qsort);
    expect_true(rows[0].is_to_me, "qsort[0] to_me");
    expect_true(rows[1].is_cq && rows[1].snr == -5, "qsort[1] strong cq");
    expect_true(rows[2].is_cq && rows[2].snr == -18, "qsort[2] weak cq");
    expect_true(!rows[3].is_cq && !rows[3].is_recent_qso && !rows[3].is_to_me, "qsort[3] other");
    expect_true(rows[4].is_recent_qso, "qsort[4] recent");

    if (g_fails != 0) {
        fprintf(stderr, "%d failure(s)\n", g_fails);
        return 1;
    }
    printf("PASS: decode sort\n");
    return 0;
}
