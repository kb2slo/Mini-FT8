/*
 * Assert: R-screen touch does not duplicate the same DX in the TX queue.
 * - Second touch of the same CQ promotes to front (one entry).
 * - Touch while that call is the live queue head is ignored.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>

#include "autoseq.h"
#include "host_mocks.h"

static UiRxLine make_cq(const char* dx, int snr, int slot, int offset_hz) {
    UiRxLine m;
    m.field1 = "CQ";
    m.field2 = dx;
    m.field3 = "FN42";
    m.text = std::string("CQ ") + dx + " FN42";
    m.snr = snr;
    m.offset_hz = offset_hz;
    m.slot_id = slot;
    m.is_cq = true;
    m.is_to_me = false;
    return m;
}

static int count_dx(const char* dx) {
    int n = 0;
    const int active = autoseq_active_count();
    for (int i = 0; i < active; ++i) {
        QsoContext ctx;
        if (!autoseq_get_active_context(i, &ctx)) continue;
        if (ctx.is_freetext) continue;
        if (strcasecmp(ctx.dxcall.c_str(), dx) == 0) ++n;
    }
    return n;
}

static int fail(const char* msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main() {
    autoseq_init();
    autoseq_set_station("KB2SLO", "FN30");
    autoseq_set_max_retry(5);

    UiRxLine other = make_cq("W1AW", -10, 1, 1200);
    UiRxLine target = make_cq("K1ABC", -5, 1, 1500);
    UiRxLine target2 = make_cq("K1ABC", -2, 1, 1800);

    if (autoseq_on_touch(target) != AutoseqTouchResult::Queued) {
        return fail("queue K1ABC");
    }
    // Force another call to the front so K1ABC sits deeper in the queue.
    if (autoseq_on_touch(other) != AutoseqTouchResult::Queued) {
        return fail("queue W1AW");
    }
    if (autoseq_active_count() != 2) {
        return fail("expected 2 active after two touches");
    }
    if (count_dx("K1ABC") != 1) {
        return fail("K1ABC should appear once after first touch");
    }
    QsoContext head;
    if (!autoseq_get_active_context(0, &head) ||
        strcasecmp(head.dxcall.c_str(), "W1AW") != 0) {
        return fail("W1AW should be at front before re-touch");
    }

    // Re-touch K1ABC: drop the deeper entry, promote fresh tap to front.
    if (autoseq_on_touch(target2) != AutoseqTouchResult::Queued) {
        return fail("re-touch K1ABC should queue/promote");
    }
    if (count_dx("K1ABC") != 1) {
        return fail("K1ABC duplicated after re-touch");
    }
    if (autoseq_active_count() != 2) {
        return fail("expected still 2 active (W1AW + one K1ABC)");
    }

    if (!autoseq_get_active_context(0, &head) ||
        strcasecmp(head.dxcall.c_str(), "K1ABC") != 0) {
        return fail("re-touch should force K1ABC to queue front");
    }
    if (head.offset_hz != 1800 || head.snr_tx != -2) {
        return fail("promoted entry should use latest tap offset/snr");
    }

    // Live QSO at head: ignore duplicate tap.
    if (autoseq_on_touch(target) != AutoseqTouchResult::IgnoredInProgress) {
        return fail("live head K1ABC should ignore re-touch");
    }
    if (count_dx("K1ABC") != 1 || autoseq_active_count() != 2) {
        return fail("ignore path mutated the queue");
    }

    printf("PASS: unique-callsign touch dedupe/promote\n");
    return 0;
}
