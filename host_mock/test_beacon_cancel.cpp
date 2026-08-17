/*
 * Beacon off must drop a queued CQ immediately so it does not still TX.
 * QSOs in the queue are left alone.
 */
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "autoseq.h"
#include "host_mocks.h"

static int fail(const char* msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main() {
    autoseq_init();
    autoseq_set_station("KB2SLO", "FN30");

    autoseq_start_cq(0);
    AutoseqTxEntry pending;
    if (!autoseq_fetch_pending_tx(pending) || pending.dxcall != "CQ") {
        return fail("expected queued beacon CQ");
    }

    if (!autoseq_cancel_cq()) {
        return fail("cancel should remove the CQ");
    }
    if (autoseq_fetch_pending_tx(pending)) {
        return fail("queue should be empty after beacon-off cancel");
    }
    if (autoseq_active_count() != 0) {
        return fail("no active entries after cancel");
    }

    autoseq_start_cq(0);
    UiRxLine cq;
    cq.field1 = "CQ";
    cq.field2 = "K1ABC";
    cq.field3 = "FN42";
    cq.snr = -5;
    cq.offset_hz = 1500;
    cq.slot_id = 1;
    cq.is_cq = true;
    if (autoseq_on_touch(cq) != AutoseqTouchResult::Queued) {
        return fail("queue K1ABC QSO");
    }
    if (autoseq_active_count() != 2) {
        return fail("expected CQ + QSO");
    }
    if (!autoseq_cancel_cq()) {
        return fail("cancel should drop CQ beside the QSO");
    }
    if (autoseq_active_count() != 1) {
        return fail("QSO should remain");
    }
    QsoContext head;
    if (!autoseq_get_active_context(0, &head) ||
        strcasecmp(head.dxcall.c_str(), "K1ABC") != 0) {
        return fail("remaining head should be K1ABC");
    }

    printf("PASS: beacon-off cancels queued CQ\n");
    return 0;
}
