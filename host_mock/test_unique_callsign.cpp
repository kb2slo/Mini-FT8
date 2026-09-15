/*
 * Assert: R-screen touch does not duplicate the same DX in the TX queue.
 * - Second touch of the same CQ promotes to front (one entry).
 * - Touch while that call is the live queue head is ignored.
 * - QsoContext::entry_id (RFC 0004 §11 QUEUE_ENTRY/QUEUE_CANCEL): distinct
 *   per real entry, stable across a reshuffle that moves the same contact,
 *   fresh for a re-touch that actually drops and re-creates one, and
 *   autoseq_drop_by_entry_id removes the right one and nothing else.
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
    const uint16_t w1aw_id = head.entry_id;
    QsoContext deeper;
    if (!autoseq_get_active_context(1, &deeper) ||
        strcasecmp(deeper.dxcall.c_str(), "K1ABC") != 0) {
        return fail("K1ABC should be behind W1AW before re-touch");
    }
    const uint16_t k1abc_id_before = deeper.entry_id;
    if (w1aw_id == 0 || k1abc_id_before == 0) {
        return fail("entry_id must be non-zero for a real entry");
    }
    if (w1aw_id == k1abc_id_before) {
        return fail("distinct entries must have distinct entry_id");
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
    // Re-touch actually drops and re-creates the K1ABC context (the "deeper
    // entry" comment above), so its entry_id must change -- a stale id from
    // before the re-touch must not still resolve.
    if (head.entry_id == k1abc_id_before) {
        return fail("re-touched entry should get a fresh entry_id, not reuse the old one");
    }
    if (head.entry_id == 0) {
        return fail("re-touched entry must still get a real entry_id");
    }
    QsoContext still_w1aw;
    if (!autoseq_get_active_context(1, &still_w1aw) ||
        strcasecmp(still_w1aw.dxcall.c_str(), "W1AW") != 0) {
        return fail("W1AW should now be behind the re-touched K1ABC");
    }
    if (still_w1aw.entry_id != w1aw_id) {
        return fail("W1AW's entry_id must survive being moved by the reshuffle");
    }

    // Live QSO at head: ignore duplicate tap.
    if (autoseq_on_touch(target) != AutoseqTouchResult::IgnoredInProgress) {
        return fail("live head K1ABC should ignore re-touch");
    }
    if (count_dx("K1ABC") != 1 || autoseq_active_count() != 2) {
        return fail("ignore path mutated the queue");
    }

    // During TX the live head must stay put; a new tap queues behind it.
    autoseq_init();
    autoseq_set_station("KB2SLO", "FN30");
    autoseq_set_max_retry(5);
    if (autoseq_on_touch(other) != AutoseqTouchResult::Queued) {
        return fail("hold-head setup W1AW");
    }
    if (autoseq_on_touch(target, true) != AutoseqTouchResult::Queued) {
        return fail("hold-head queue K1ABC behind W1AW");
    }
    if (!autoseq_get_active_context(0, &head) ||
        strcasecmp(head.dxcall.c_str(), "W1AW") != 0) {
        return fail("hold-head must keep W1AW at front");
    }
    QsoContext behind;
    if (!autoseq_get_active_context(1, &behind) ||
        strcasecmp(behind.dxcall.c_str(), "K1ABC") != 0) {
        return fail("hold-head should place K1ABC next");
    }

    // autoseq_drop_by_entry_id: cancels the right entry by stable id, not by
    // whatever position it currently occupies.
    autoseq_init();
    autoseq_set_station("KB2SLO", "FN30");
    autoseq_set_max_retry(5);
    if (autoseq_on_touch(other) != AutoseqTouchResult::Queued) {
        return fail("drop_by_entry_id setup: queue W1AW");
    }
    if (autoseq_on_touch(target) != AutoseqTouchResult::Queued) {
        return fail("drop_by_entry_id setup: queue K1ABC");
    }
    QsoContext w1aw_ctx, k1abc_ctx;
    if (!autoseq_get_active_context(1, &w1aw_ctx) ||
        strcasecmp(w1aw_ctx.dxcall.c_str(), "W1AW") != 0) {
        return fail("drop_by_entry_id setup: expected W1AW behind K1ABC");
    }
    if (!autoseq_get_active_context(0, &k1abc_ctx) ||
        strcasecmp(k1abc_ctx.dxcall.c_str(), "K1ABC") != 0) {
        return fail("drop_by_entry_id setup: expected K1ABC at front");
    }

    if (autoseq_drop_by_entry_id(0)) {
        return fail("entry_id 0 (the sentinel) must never match a real entry");
    }
    if (autoseq_drop_by_entry_id(w1aw_ctx.entry_id + k1abc_ctx.entry_id + 1000)) {
        return fail("an id nothing carries must not match by accident");
    }
    if (autoseq_active_count() != 2) {
        return fail("failed cancels must not mutate the queue");
    }

    if (!autoseq_drop_by_entry_id(w1aw_ctx.entry_id)) {
        return fail("drop_by_entry_id should find W1AW regardless of its position");
    }
    if (count_dx("W1AW") != 0) {
        return fail("W1AW should be gone after drop_by_entry_id");
    }
    if (count_dx("K1ABC") != 1) {
        return fail("drop_by_entry_id must not touch the other entry");
    }
    if (autoseq_drop_by_entry_id(w1aw_ctx.entry_id)) {
        return fail("dropping the same entry_id twice should fail the second time");
    }

    printf("PASS: unique-callsign touch dedupe/promote\n");
    return 0;
}
