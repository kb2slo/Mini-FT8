// Host test for the sidekick/main frame codec (I28a, RFC 0004 §6).
//
// The codec is where a link protocol quietly goes wrong: a length off by one,
// a CRC that misses a class of errors, a decoder that treats a payload byte as
// a frame start, or one that never recovers after noise. None of that is
// visible on a bench -- it shows up as "the link is flaky" months later. So
// every one of those is pinned here, including the deliberate limitation
// (a corrupt frame can swallow the frame behind it) so that behaviour is a
// recorded decision rather than a surprise.

#include "porta_proto.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        g_fail++;
    }
}

// Feeds every byte and collects whatever frames fall out.
static std::vector<porta_frame_t> run(porta_decoder_t* d, const std::vector<uint8_t>& bytes)
{
    std::vector<porta_frame_t> got;
    porta_frame_t f;
    for (uint8_t b : bytes) {
        if (porta_decoder_push(d, b, &f)) {
            got.push_back(f);
        }
    }
    return got;
}

static std::vector<uint8_t> encode(uint8_t type, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> buf(PORTA_PROTO_MAX_FRAME);
    size_t n = porta_proto_encode(type, payload.empty() ? nullptr : payload.data(),
                                  (uint8_t)payload.size(), buf.data(), buf.size());
    buf.resize(n);
    return buf;
}

static void test_round_trip()
{
    porta_decoder_t d;
    porta_decoder_init(&d);

    std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0xFF, 0x00};
    auto wire = encode(PORTA_MSG_CONFIG_VALUE, payload);
    check(wire.size() == payload.size() + 4, "frame is payload + 4 overhead bytes");
    check(wire[0] == PORTA_PROTO_SYNC, "frame starts with the sync byte");
    check(wire[1] == PORTA_MSG_CONFIG_VALUE, "type is second");
    check(wire[2] == payload.size(), "length is third");

    auto got = run(&d, wire);
    check(got.size() == 1, "one frame in, one frame out");
    if (got.size() == 1) {
        check(got[0].type == PORTA_MSG_CONFIG_VALUE, "type round-trips");
        check(got[0].len == payload.size(), "length round-trips");
        check(std::memcmp(got[0].payload, payload.data(), payload.size()) == 0,
              "payload round-trips");
    }
    check(d.frames_ok == 1 && d.crc_errors == 0, "counters after a clean frame");
}

static void test_empty_and_max_payload()
{
    porta_decoder_t d;
    porta_decoder_init(&d);

    // len == 0 is legal: POLL and NOTHING carry nothing.
    auto empty = encode(PORTA_MSG_POLL, {});
    check(empty.size() == 4, "empty frame is 4 bytes");
    auto got = run(&d, empty);
    check(got.size() == 1 && got[0].len == 0, "zero-length payload decodes");

    // 255 is the largest length the single length byte can express.
    std::vector<uint8_t> big(PORTA_PROTO_MAX_PAYLOAD);
    for (size_t i = 0; i < big.size(); ++i) {
        big[i] = (uint8_t)(i * 7 + 3);
    }
    auto wire = encode(PORTA_MSG_FILE_DATA, big);
    check(wire.size() == PORTA_PROTO_MAX_FRAME, "max frame is 259 bytes");
    got = run(&d, wire);
    check(got.size() == 1, "max-length frame decodes");
    if (got.size() == 1) {
        check(got[0].len == PORTA_PROTO_MAX_PAYLOAD, "max length round-trips");
        check(std::memcmp(got[0].payload, big.data(), big.size()) == 0,
              "max payload round-trips");
    }
}

// The reason the decoder counts bytes instead of hunting for sync: payloads are
// binary and 0xC6 will occur in them.
static void test_sync_byte_inside_payload()
{
    porta_decoder_t d;
    porta_decoder_init(&d);

    std::vector<uint8_t> payload = {PORTA_PROTO_SYNC, PORTA_PROTO_SYNC, 0x00, PORTA_PROTO_SYNC};
    auto got = run(&d, encode(PORTA_MSG_FILE_DATA, payload));
    check(got.size() == 1, "a payload full of sync bytes is still one frame");
    if (got.size() == 1) {
        check(got[0].len == 4 && got[0].payload[0] == PORTA_PROTO_SYNC,
              "sync bytes inside a payload are data");
    }
}

// Every single-bit flip anywhere in the frame must be caught. This is the whole
// job of the CRC, and a sum-of-bytes checksum (what the old beacon used) fails
// it -- two compensating flips, or a flip in the high bit, can slip through.
static void test_crc_catches_every_single_bit_flip()
{
    std::vector<uint8_t> payload = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
    auto clean = encode(PORTA_MSG_CONFIG_VALUE, payload);

    int missed = 0;
    for (size_t byte = 0; byte < clean.size(); ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            auto corrupt = clean;
            corrupt[byte] ^= (uint8_t)(1u << bit);
            if (corrupt[0] != PORTA_PROTO_SYNC) {
                continue;  // flipping the sync byte means the frame is never started
            }
            porta_decoder_t d;
            porta_decoder_init(&d);
            if (!run(&d, corrupt).empty()) {
                missed++;
            }
        }
    }
    check(missed == 0, "CRC rejects every single-bit flip in the framed bytes");
}

static void test_truncated_frame_then_recovery()
{
    porta_decoder_t d;
    porta_decoder_init(&d);

    auto full = encode(PORTA_MSG_CONFIG_VALUE, {1, 2, 3, 4, 5, 6, 7, 8});
    std::vector<uint8_t> truncated(full.begin(), full.end() - 3);
    auto got = run(&d, truncated);
    check(got.empty(), "a truncated frame yields nothing");

    // The decoder is mid-payload, so the next frame's sync is consumed as
    // payload -- the truncated frame swallows it, exactly as documented. What
    // matters is that it recovers rather than wedging.
    got = run(&d, full);
    got = run(&d, full);
    check(!got.empty(), "decoder recovers after a truncated frame");
}

static void test_garbage_before_a_frame()
{
    porta_decoder_t d;
    porta_decoder_init(&d);

    std::vector<uint8_t> stream = {'$', 'G', 'P', 'G', 'G', 'A', ',', '1', '2', '\r', '\n'};
    auto frame = encode(PORTA_MSG_HELLO, std::vector<uint8_t>(PORTA_HELLO_PAYLOAD_LEN, 0x41));
    stream.insert(stream.end(), frame.begin(), frame.end());

    auto got = run(&d, stream);
    check(got.size() == 1, "NMEA-looking garbage before a frame is skipped");
    check(d.crc_errors == 0, "clean garbage costs no CRC error");
}

// The documented limitation, pinned so it stays a decision. Noise containing a
// stray sync byte starts a false frame that eats the real one behind it; the
// frame after that is found.
static void test_false_sync_eats_one_frame_then_recovers()
{
    porta_decoder_t d;
    porta_decoder_init(&d);

    std::vector<uint8_t> stream = {PORTA_PROTO_SYNC, 0x99, 0x04, 0xAA};  // bogus header
    auto frame = encode(PORTA_MSG_POLL, {});
    stream.insert(stream.end(), frame.begin(), frame.end());
    stream.insert(stream.end(), frame.begin(), frame.end());

    auto got = run(&d, stream);
    check(got.size() == 1, "a false sync costs exactly one real frame");
    check(d.crc_errors == 1, "the false frame is counted as a CRC error");
}

static void test_encode_refuses_a_short_buffer()
{
    uint8_t small[4];
    std::vector<uint8_t> payload = {1, 2, 3, 4, 5};
    check(porta_proto_encode(PORTA_MSG_CONFIG_VALUE, payload.data(), (uint8_t)payload.size(),
                             small, sizeof(small)) == 0,
          "encode refuses rather than writing a partial frame");

    uint8_t exact[4];
    check(porta_proto_encode(PORTA_MSG_POLL, nullptr, 0, exact, sizeof(exact)) == 4,
          "a buffer of exactly the needed size is accepted");
}

// Counters must survive the decoder's own internal resync after a bad frame --
// otherwise a wire quietly failing half its frames looks healthy. They must NOT
// survive an explicit init, which means "this is a new link".
static void test_counters()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    check(d.frames_ok == 0 && d.crc_errors == 0, "init zeroes the counters");

    run(&d, encode(PORTA_MSG_POLL, {}));
    check(d.frames_ok == 1, "one good frame counted");

    auto bad = encode(PORTA_MSG_POLL, {});
    bad.back() ^= 0xFF;
    run(&d, bad);
    check(d.crc_errors == 1, "a bad frame is counted");
    check(d.frames_ok == 1, "an internal resync keeps the good-frame count");

    run(&d, encode(PORTA_MSG_POLL, {}));
    check(d.frames_ok == 2, "decoder still works after a bad frame");

    porta_decoder_init(&d);
    check(d.frames_ok == 0 && d.crc_errors == 0, "an explicit init starts a new link");
}

static void test_hello_round_trip()
{
    porta_decoder_t d;
    porta_decoder_init(&d);

    uint8_t buf[PORTA_PROTO_MAX_FRAME];
    size_t n = porta_proto_encode_hello(PORTA_PROTO_VERSION, "f0c79d3-dirty", buf, sizeof(buf));
    check(n == PORTA_HELLO_PAYLOAD_LEN + 4, "hello frame size");

    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "hello decodes");
    if (got.size() == 1) {
        uint8_t ver = 0;
        char build[PORTA_HELLO_VERSION_LEN + 1] = {};
        check(porta_proto_parse_hello(&got[0], &ver, build), "hello payload parses");
        check(ver == PORTA_PROTO_VERSION, "protocol version round-trips");
        check(std::string(build) == "f0c79d3-dirty", "build version round-trips");
    }

    // A version that fills all 32 bytes has no room for a terminator on the
    // wire; the decoder must still hand back a usable C string.
    std::string full(PORTA_HELLO_VERSION_LEN, 'x');
    n = porta_proto_encode_hello(7, full.c_str(), buf, sizeof(buf));
    porta_decoder_init(&d);
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "hello with a full-width version decodes");
    if (got.size() == 1) {
        uint8_t ver = 0;
        char build[PORTA_HELLO_VERSION_LEN + 1] = {};
        check(porta_proto_parse_hello(&got[0], &ver, build), "full-width hello parses");
        check(std::string(build) == full, "a 32-character version is not truncated");
    }
}

static void test_hello_rejects_wrong_shape()
{
    porta_frame_t f = {};
    f.type = PORTA_MSG_CONFIG_VALUE;
    f.len = PORTA_HELLO_PAYLOAD_LEN;
    check(!porta_proto_parse_hello(&f, nullptr, nullptr), "wrong type is not a hello");

    f.type = PORTA_MSG_HELLO;
    f.len = 4;
    check(!porta_proto_parse_hello(&f, nullptr, nullptr), "wrong length is not a hello");
}

// EVENT is a namespace: subtype first, body after. These pin the field packing
// and, more importantly, the boundaries -- an empty text, a full-width text, and
// a frame one byte too short to hold the fixed part.
static void test_log_event()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    size_t n = porta_proto_encode_log(1789000000u, "Queued: CQ KB2SLO FN30", buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "log event decodes");
    if (got.size() == 1) {
        char text[PORTA_EVENT_TEXT_MAX + 1] = {};
        uint32_t ts = 0;
        check(porta_proto_parse_log(&got[0], &ts, text), "log event parses");
        check(ts == 1789000000u, "timestamp round-trips through the envelope");
        check(std::string(text) == "Queued: CQ KB2SLO FN30", "log text round-trips");
        // A decode parser must reject a log event and vice versa: same frame
        // type, different subtype, and confusing them would silently misread
        // the first seven bytes of a message as numeric fields.
        porta_decode_event_t dec;
        check(!porta_proto_parse_decode(&got[0], &dec), "a log event is not a decode event");
    }

    // Empty text is legal -- a zero-length log line is odd but not malformed.
    porta_decoder_init(&d);
    n = porta_proto_encode_log(0, "", buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1 && got[0].len == 5, "empty log event is envelope only");

    // Longer than the field: truncated, not overflowed.
    porta_decoder_init(&d);
    std::string longtext(PORTA_EVENT_TEXT_MAX + 40, 'z');
    n = porta_proto_encode_log(42u, longtext.c_str(), buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "over-long log event still encodes");
    if (got.size() == 1) {
        char text[PORTA_EVENT_TEXT_MAX + 1] = {};
        porta_proto_parse_log(&got[0], nullptr, text);
        check(std::strlen(text) == PORTA_EVENT_TEXT_MAX, "over-long text is truncated to the field");
    }
}

// Zero is "the host does not know the time", which happens on a cold unit with
// no GPS and no DS3231. It must survive as zero rather than being rejected or
// substituted, because the viewer needs to tell "no clock" from "midnight".
static void test_unset_clock_round_trips()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];
    size_t n = porta_proto_encode_log(0, "no clock yet", buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "unset-clock log event decodes");
    if (got.size() == 1) {
        uint32_t ts = 12345;
        char text[PORTA_EVENT_TEXT_MAX + 1] = {};
        check(porta_proto_parse_log(&got[0], &ts, text), "unset-clock event parses");
        check(ts == 0, "a zero timestamp stays zero");
    }
}

static void test_decode_event()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    porta_decode_event_t in = {};
    std::snprintf(in.text, sizeof(in.text), "CQ DX W1AW FN31");
    in.epoch_secs = 1789012345u;
    in.decode_id = 424242u;    // non-zero, to catch a field that silently reads as zero
    in.snr = -21;              // negative, to catch an unsigned round-trip
    in.offset_hz = 2750;       // above 2047, to catch a truncated width
    in.dt_centis = -145;       // negative, likewise
    in.is_cq = true;
    in.is_recent_qso = true;

    size_t n = porta_proto_encode_decode(&in, buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "decode event decodes");
    if (got.size() == 1) {
        porta_decode_event_t out;
        check(porta_proto_parse_decode(&got[0], &out), "decode event parses");
        check(std::string(out.text) == "CQ DX W1AW FN31", "text round-trips");
        check(out.decode_id == 424242u, "decode_id round-trips");
        check(out.snr == -21, "negative SNR round-trips");
        check(out.offset_hz == 2750, "offset above 2047 round-trips");
        check(out.dt_centis == -145, "negative dt round-trips");
        check(out.epoch_secs == 1789012345u, "decode timestamp round-trips");
        check(out.is_cq && out.is_recent_qso && !out.is_to_me, "flags round-trip independently");

        char text[PORTA_EVENT_TEXT_MAX + 1] = {};
        check(!porta_proto_parse_log(&got[0], nullptr, text), "a decode event is not a log event");
    }

    // A frame carrying the subtype but not the fixed fields must be rejected
    // rather than read past its own length.
    porta_frame_t stub = {};
    stub.type = PORTA_MSG_EVENT;
    stub.len = 8;   // envelope plus part of the fixed body
    stub.payload[0] = PORTA_EVT_DECODE;
    porta_decode_event_t out;
    check(!porta_proto_parse_decode(&stub, &out), "a short decode event is rejected");
}

// QUEUE_ENTRY reports one autoseq context. entry_id is the load-bearing field:
// it must survive independently of state/retry/dxcall, since QUEUE_CANCEL
// addresses by this id rather than by list position (the queue re-sorts by
// priority, so a position is not safe to cancel by).
static void test_queue_entry_event()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    porta_queue_entry_event_t in = {};
    in.epoch_secs = 1789012345u;
    in.entry_id = 4242;
    in.state = 1;            // REPLYING
    in.retry_count = 2;
    in.retry_limit = 5;
    std::snprintf(in.dxcall, sizeof(in.dxcall), "KB2SLO/P");  // exercise a portable suffix

    size_t n = porta_proto_encode_queue_entry(&in, buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "queue_entry event decodes");
    if (got.size() == 1) {
        porta_queue_entry_event_t out;
        check(porta_proto_parse_queue_entry(&got[0], &out), "queue_entry event parses");
        check(out.entry_id == 4242, "entry_id round-trips");
        check(out.state == 1, "state round-trips");
        check(out.retry_count == 2 && out.retry_limit == 5, "retry counters round-trip");
        check(std::string(out.dxcall) == "KB2SLO/P", "dxcall with a portable suffix round-trips");

        porta_decode_event_t dec;
        check(!porta_proto_parse_decode(&got[0], &dec), "a queue_entry event is not a decode event");
    }

    // IDLE (state 6) is the removal signal, not a separate flag -- must round
    // trip like any other state rather than being special-cased away.
    porta_decoder_init(&d);
    in.state = 6;
    n = porta_proto_encode_queue_entry(&in, buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    if (got.size() == 1) {
        porta_queue_entry_event_t out;
        check(porta_proto_parse_queue_entry(&got[0], &out) && out.state == 6,
              "IDLE state round-trips like any other -- it is the removal signal");
    }

    // A dxcall_len claiming more bytes than the frame actually carries must be
    // rejected rather than read past the payload.
    porta_frame_t stub = {};
    stub.type = PORTA_MSG_EVENT;
    stub.len = 11;  // fixed part only
    stub.payload[0] = PORTA_EVT_QUEUE_ENTRY;
    stub.payload[10] = 5;  // claims a 5-byte dxcall that is not there
    porta_queue_entry_event_t out;
    check(!porta_proto_parse_queue_entry(&stub, &out), "a truncated dxcall is rejected");
}

static void test_slot_state_event()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    porta_slot_state_event_t in = {};
    in.epoch_secs = 1789012345u;
    in.slot_parity = 1;
    in.beacon_mode = 2;  // ODD
    in.resolved_offset_hz = 1834;

    size_t n = porta_proto_encode_slot_state(&in, buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "slot_state event decodes");
    if (got.size() == 1) {
        porta_slot_state_event_t out;
        check(porta_proto_parse_slot_state(&got[0], &out), "slot_state event parses");
        check(out.slot_parity == 1, "slot_parity round-trips");
        check(out.beacon_mode == 2, "beacon_mode round-trips");
        check(out.resolved_offset_hz == 1834, "resolved_offset_hz round-trips");

        porta_queue_entry_event_t qe;
        check(!porta_proto_parse_queue_entry(&got[0], &qe),
              "a slot_state event is not a queue_entry event");
    }
}

static void test_tx_hud_event()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    porta_tx_hud_event_t in = {};
    in.epoch_secs = 1789012345u;
    in.active = true;
    in.aborted = false;
    in.writes_blocked = false;
    in.power_dw = 42;   // 4.2 W
    in.swr_c = 135;     // 1.35
    in.battery_pct = 87;
    std::snprintf(in.text, sizeof(in.text), "CQ KB2SLO FN30");

    size_t n = porta_proto_encode_tx_hud(&in, buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "tx_hud event decodes");
    if (got.size() == 1) {
        porta_tx_hud_event_t out;
        check(porta_proto_parse_tx_hud(&got[0], &out), "tx_hud event parses");
        check(out.active && !out.aborted && !out.writes_blocked, "flags round-trip");
        check(out.power_dw == 42, "power_dw round-trips");
        check(out.swr_c == 135, "swr_c round-trips");
        check(out.battery_pct == 87, "battery_pct round-trips");
        check(std::string(out.text) == "CQ KB2SLO FN30", "text round-trips");
        check(std::string(out.reason).empty(), "reason empty when not aborted");

        porta_slot_state_event_t ss;
        check(!porta_proto_parse_slot_state(&got[0], &ss),
              "a tx_hud event is not a slot_state event");
    }

    // -1 sentinels (unread power/SWR/battery) must survive the cast through
    // uint16_t/uint8_t on the wire and back, same as rst_sent/rst_rcvd's -99.
    // The abort reason must survive alongside the message text -- both are
    // real fields now, not one hardcoded client-side.
    porta_tx_hud_event_t unknown = {};
    unknown.active = false;
    unknown.aborted = true;
    unknown.writes_blocked = true;
    unknown.power_dw = -1;
    unknown.swr_c = -1;
    unknown.battery_pct = -1;
    std::snprintf(unknown.reason, sizeof(unknown.reason), "low battery");
    std::snprintf(unknown.text, sizeof(unknown.text), "CQ KB2SLO FN30");
    porta_decoder_init(&d);
    n = porta_proto_encode_tx_hud(&unknown, buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    if (got.size() == 1) {
        porta_tx_hud_event_t out;
        check(porta_proto_parse_tx_hud(&got[0], &out) &&
              !out.active && out.aborted && out.writes_blocked &&
              out.power_dw == -1 && out.swr_c == -1 && out.battery_pct == -1 &&
              std::string(out.reason) == "low battery" &&
              std::string(out.text) == "CQ KB2SLO FN30",
              "unknown sentinels, abort reason, and text round-trip together");
    }
}

// ACTION is the control direction, and the first thing to cross it is the
// clock. These pin the round trip and, more importantly, that a reply can be
// matched to its request by verb -- there is no sequence number, so a NAK that
// forgot which verb it was answering would be unattributable.
static void test_action_set_clock()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    size_t n = porta_proto_encode_set_clock(1789012345u, 750, buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "set_clock decodes");
    if (got.size() == 1) {
        uint32_t secs = 0; uint16_t ms = 0;
        check(porta_proto_parse_set_clock(&got[0], &secs, &ms), "set_clock parses");
        check(secs == 1789012345u, "epoch seconds round-trip");
        check(ms == 750, "milliseconds round-trip");
    }

    // Wrong length must be refused rather than read past the payload.
    porta_frame_t stub = {};
    stub.type = PORTA_MSG_ACTION;
    stub.len = 3;
    stub.payload[0] = PORTA_ACT_SET_CLOCK;
    uint32_t secs = 0;
    check(!porta_proto_parse_set_clock(&stub, &secs, nullptr), "a short set_clock is refused");
}

static void test_action_tx_free_and_cancel()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    size_t n = porta_proto_encode_tx_free("CQ KB2SLO FN30", buf, sizeof(buf));
    check(n > 0, "tx_free encodes");
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "tx_free decodes");
    if (got.size() == 1) {
        char text[PORTA_EVENT_TEXT_MAX + 1] = {};
        check(porta_proto_parse_tx_free(&got[0], text), "tx_free parses");
        check(std::string(text) == "CQ KB2SLO FN30", "tx_free text round-trips");
        check(!porta_proto_parse_tx_cancel(&got[0]), "tx_free is not cancel");
        check(!porta_proto_parse_set_clock(&got[0], nullptr, nullptr), "tx_free is not set_clock");
    }

    check(porta_proto_encode_tx_free("", buf, sizeof(buf)) == 0, "empty tx_free is refused at encode");
    check(porta_proto_encode_tx_free(nullptr, buf, sizeof(buf)) == 0, "null tx_free is refused");

    // Longer than the field is truncated rather than rejected: the frame length
    // is the source of truth, and FT8 will NAK at the host if encode fails.
    std::string long_text(PORTA_EVENT_TEXT_MAX + 8, 'A');
    porta_decoder_init(&d);
    n = porta_proto_encode_tx_free(long_text.c_str(), buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "an overlong tx_free still frames");
    if (got.size() == 1) {
        char text[PORTA_EVENT_TEXT_MAX + 1] = {};
        check(porta_proto_parse_tx_free(&got[0], text), "overlong tx_free parses");
        check(std::strlen(text) == PORTA_EVENT_TEXT_MAX, "overlong text is capped to the field");
    }

    porta_decoder_init(&d);
    n = porta_proto_encode_tx_cancel(buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1 && got[0].len == 1, "tx_cancel is verb-only");
    if (got.size() == 1) {
        check(porta_proto_parse_tx_cancel(&got[0]), "tx_cancel parses");
        char text[PORTA_EVENT_TEXT_MAX + 1] = {};
        check(!porta_proto_parse_tx_free(&got[0], text), "tx_cancel is not tx_free");
    }
}

static void test_action_connect_and_tune()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    size_t n = porta_proto_encode_connect(buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1 && got[0].len == 1, "connect is verb-only");
    if (got.size() == 1) {
        check(porta_proto_parse_connect(&got[0]), "connect parses");
        check(!porta_proto_parse_tx_cancel(&got[0]), "connect is not cancel");
        bool on = true;
        check(!porta_proto_parse_tune(&got[0], &on), "connect is not tune");
    }

    porta_decoder_init(&d);
    n = porta_proto_encode_tune(true, buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1 && got[0].len == 2, "tune is verb + on");
    if (got.size() == 1) {
        bool on = false;
        check(porta_proto_parse_tune(&got[0], &on), "tune on parses");
        check(on, "tune on is true");
        check(!porta_proto_parse_connect(&got[0]), "tune is not connect");
    }

    porta_decoder_init(&d);
    n = porta_proto_encode_tune(false, buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "tune off frames");
    if (got.size() == 1) {
        bool on = true;
        check(porta_proto_parse_tune(&got[0], &on) && !on, "tune off is false");
    }
}

static void test_action_beacon()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    // Mode is BeaconMode's own wire value (0=OFF/1=EVEN/2=ODD) -- pin all
    // three so a translation slip in either direction shows up here rather
    // than as a beacon that starts on the wrong parity in the field.
    for (uint8_t mode = 0; mode <= 2; ++mode) {
        porta_decoder_init(&d);
        size_t n = porta_proto_encode_beacon(mode, buf, sizeof(buf));
        auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
        check(got.size() == 1 && got[0].len == 2, "beacon is verb + mode");
        if (got.size() == 1) {
            uint8_t out = 99;
            check(porta_proto_parse_beacon(&got[0], &out) && out == mode,
                  "beacon mode round-trips");
        }
    }

    porta_decoder_init(&d);
    size_t n = porta_proto_encode_beacon(1, buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    if (got.size() == 1) {
        check(!porta_proto_parse_tune(&got[0], nullptr), "beacon is not tune");
    }
}

static void test_action_queue_cancel_and_reply()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    size_t n = porta_proto_encode_queue_cancel(4242, buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1 && got[0].len == 3, "queue_cancel is verb + u16 entry_id");
    if (got.size() == 1) {
        uint16_t id = 0;
        check(porta_proto_parse_queue_cancel(&got[0], &id) && id == 4242,
              "queue_cancel entry_id round-trips");
        check(!porta_proto_parse_beacon(&got[0], nullptr), "queue_cancel is not beacon");
    }

    porta_decoder_init(&d);
    n = porta_proto_encode_queue_reply(0xDEADBEEFu, buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1 && got[0].len == 5, "queue_reply is verb + u32 decode_id");
    if (got.size() == 1) {
        uint32_t id = 0;
        check(porta_proto_parse_queue_reply(&got[0], &id) && id == 0xDEADBEEFu,
              "queue_reply decode_id round-trips, including the high bit");
        check(!porta_proto_parse_queue_cancel(&got[0], nullptr), "queue_reply is not queue_cancel");
    }
}

static void test_ack_and_nak()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    size_t n = porta_proto_encode_ack(PORTA_ACT_SET_CLOCK, buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "ack decodes");
    if (got.size() == 1) {
        uint8_t verb = 0;
        check(porta_proto_parse_ack(&got[0], &verb), "ack parses");
        check(verb == PORTA_ACT_SET_CLOCK, "ack carries the verb it answers");
        char reason[PORTA_EVENT_TEXT_MAX + 1] = {};
        check(!porta_proto_parse_nak(&got[0], nullptr, reason), "an ack is not a nak");
    }

    porta_decoder_init(&d);
    n = porta_proto_encode_nak(PORTA_ACT_SET_CLOCK, "clock is read-only", buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "nak decodes");
    if (got.size() == 1) {
        uint8_t verb = 0;
        char reason[PORTA_EVENT_TEXT_MAX + 1] = {};
        check(porta_proto_parse_nak(&got[0], &verb, reason), "nak parses");
        check(verb == PORTA_ACT_SET_CLOCK, "nak carries the verb it answers");
        check(std::string(reason) == "clock is read-only", "nak reason round-trips");
        check(!porta_proto_parse_ack(&got[0], nullptr), "a nak is not an ack");
    }

    // A reason is optional; the verb alone is a valid refusal.
    porta_decoder_init(&d);
    n = porta_proto_encode_nak(PORTA_ACT_SET_CLOCK, nullptr, buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1 && got[0].len == 1, "a reasonless nak is one byte");
}

static void test_config_kv()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    size_t n = porta_proto_encode_config_get("", buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "config get-all encodes");
    if (got.size() == 1) {
        char key[PORTA_CONFIG_KEY_MAX + 1] = {'x'};
        check(porta_proto_parse_config_get(&got[0], key), "get-all parses");
        check(key[0] == '\0', "get-all key is empty");
    }

    porta_decoder_init(&d);
    n = porta_proto_encode_config_set("call", "KB2SLO", buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "config set encodes");
    if (got.size() == 1) {
        char key[PORTA_CONFIG_KEY_MAX + 1] = {};
        char value[PORTA_CONFIG_VALUE_MAX + 1] = {};
        check(porta_proto_parse_config_set(&got[0], key, value), "set parses");
        check(std::string(key) == "call", "set key");
        check(std::string(value) == "KB2SLO", "set value");
        check(!porta_proto_parse_config_get(&got[0], key), "set is not get");
    }

    porta_decoder_init(&d);
    n = porta_proto_encode_config_value("grid", "FN20", buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "config value encodes");
    if (got.size() == 1) {
        char key[PORTA_CONFIG_KEY_MAX + 1] = {};
        char value[PORTA_CONFIG_VALUE_MAX + 1] = {};
        check(porta_proto_parse_config_value(&got[0], key, value), "value parses");
        check(std::string(key) == "grid" && std::string(value) == "FN20", "value kv");
    }

    check(porta_proto_encode_config_set("", "x", buf, sizeof(buf)) == 0, "set refuses empty key");
    check(porta_proto_encode_config_set("call", nullptr, buf, sizeof(buf)) == 0,
          "set refuses null value");
}

// FILE_LIST / FILE_READ page like main/storage/qso_browse.h's QsoBrowsePager
// (skip/take), and FILE_DATA carries one row per frame -- a NAME row for
// FILE_LIST, an ENTRY row for FILE_READ, discriminated by the same
// first-payload-byte pattern EVENT and ACTION already use.
static void test_file_list_and_read_requests()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    size_t n = porta_proto_encode_file_list_req(PORTA_FILE_LIST_QSO_DAILY, 3, 6, buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "file_list request encodes");
    if (got.size() == 1) {
        uint8_t kind = 0; uint16_t skip = 0; uint8_t take = 0;
        check(porta_proto_parse_file_list_req(&got[0], &kind, &skip, &take),
              "file_list request parses");
        check(kind == PORTA_FILE_LIST_QSO_DAILY, "kind round-trips");
        check(skip == 3 && take == 6, "skip/take round-trip");
    }

    porta_decoder_init(&d);
    n = porta_proto_encode_file_read_req("20260914.adi", 12, 6, buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "file_read request encodes");
    if (got.size() == 1) {
        char name[PORTA_FILENAME_MAX + 1] = {};
        uint16_t skip = 0; uint8_t take = 0;
        check(porta_proto_parse_file_read_req(&got[0], name, &skip, &take),
              "file_read request parses");
        check(std::string(name) == "20260914.adi", "filename round-trips");
        check(skip == 12 && take == 6, "file_read skip/take round-trip");
    }

    check(porta_proto_encode_file_read_req("", 0, 6, buf, sizeof(buf)) == 0,
          "file_read refuses an empty filename");
    check(porta_proto_encode_file_read_req(nullptr, 0, 6, buf, sizeof(buf)) == 0,
          "file_read refuses a null filename");
}

static void test_file_data_rows()
{
    porta_decoder_t d;
    porta_decoder_init(&d);
    uint8_t buf[PORTA_PROTO_MAX_FRAME];

    size_t n = porta_proto_encode_file_name_row("20260914.adi", buf, sizeof(buf));
    auto got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "file_name row encodes");
    if (got.size() == 1) {
        char name[PORTA_FILENAME_MAX + 1] = {};
        check(porta_proto_parse_file_name_row(&got[0], name), "file_name row parses");
        check(std::string(name) == "20260914.adi", "name round-trips");

        porta_qso_entry_row_t entry;
        check(!porta_proto_parse_file_entry_row(&got[0], &entry),
              "a name row is not an entry row");
    }

    porta_qso_entry_row_t in = {};
    std::snprintf(in.time_on, sizeof(in.time_on), "19:04");
    std::snprintf(in.band, sizeof(in.band), "20m");
    std::snprintf(in.call, sizeof(in.call), "W1ABC");
    in.rst_sent = 3;
    in.rst_rcvd = -11;
    std::snprintf(in.grid, sizeof(in.grid), "FN42");
    std::snprintf(in.freq, sizeof(in.freq), "14.074");
    std::snprintf(in.my_grid, sizeof(in.my_grid), "FN30");
    std::snprintf(in.comment, sizeof(in.comment), "nice sigs, tnx QSO");
    std::snprintf(in.mode, sizeof(in.mode), "FT8");
    std::snprintf(in.station_callsign, sizeof(in.station_callsign), "KB2SLO");

    porta_decoder_init(&d);
    n = porta_proto_encode_file_entry_row(&in, buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    check(got.size() == 1, "file_entry row encodes");
    if (got.size() == 1) {
        porta_qso_entry_row_t out;
        check(porta_proto_parse_file_entry_row(&got[0], &out), "file_entry row parses");
        check(std::string(out.time_on) == "19:04", "time_on round-trips");
        check(std::string(out.band) == "20m", "band round-trips");
        check(std::string(out.call) == "W1ABC", "call round-trips");
        check(out.rst_sent == 3 && out.rst_rcvd == -11, "rst values round-trip, including negative");
        check(std::string(out.grid) == "FN42", "grid round-trips");
        check(std::string(out.freq) == "14.074", "freq round-trips");
        check(std::string(out.my_grid) == "FN30", "my_grid round-trips");
        check(std::string(out.comment) == "nice sigs, tnx QSO", "comment round-trips");
        check(std::string(out.mode) == "FT8", "mode round-trips");
        check(std::string(out.station_callsign) == "KB2SLO", "station_callsign round-trips");
    }

    // Empty grid/freq/my_grid/comment (not every QSO logs all of these, and
    // legacy .txt logs have none) must round-trip as empty, not crash or
    // desync the frame. mode/station_callsign are required ADIF fields in
    // practice but the wire format itself does not refuse an empty one.
    porta_qso_entry_row_t no_grid = in;
    no_grid.grid[0] = '\0';
    no_grid.freq[0] = '\0';
    no_grid.my_grid[0] = '\0';
    no_grid.comment[0] = '\0';
    no_grid.mode[0] = '\0';
    no_grid.station_callsign[0] = '\0';
    porta_decoder_init(&d);
    n = porta_proto_encode_file_entry_row(&no_grid, buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    if (got.size() == 1) {
        porta_qso_entry_row_t out;
        check(porta_proto_parse_file_entry_row(&got[0], &out) &&
              std::string(out.grid).empty() && std::string(out.freq).empty() &&
              std::string(out.my_grid).empty() && std::string(out.comment).empty() &&
              std::string(out.mode).empty() && std::string(out.station_callsign).empty(),
              "empty grid/freq/my_grid/comment/mode/station_callsign round-trip");
    }

    // No report uses -99, the same sentinel QsoContext::snr_tx/snr_rx already
    // use -- must survive round-trip like any other value, not be special-cased.
    porta_decoder_init(&d);
    in.rst_sent = -99;
    in.rst_rcvd = -99;
    n = porta_proto_encode_file_entry_row(&in, buf, sizeof(buf));
    got = run(&d, std::vector<uint8_t>(buf, buf + n));
    if (got.size() == 1) {
        porta_qso_entry_row_t out;
        check(porta_proto_parse_file_entry_row(&got[0], &out) &&
              out.rst_sent == -99 && out.rst_rcvd == -99,
              "the no-report sentinel round-trips");
    }

    // qso_browse.cpp always yields "HH:MM" or "??:??" -- a caller passing
    // anything else is a programming error, not a wire condition, and must
    // be refused at encode rather than silently truncated or padded.
    porta_qso_entry_row_t bad_time = in;
    std::snprintf(bad_time.time_on, sizeof(bad_time.time_on), "1");
    check(porta_proto_encode_file_entry_row(&bad_time, buf, sizeof(buf)) == 0,
          "encode refuses a malformed time_on");
}

int main()
{
    test_round_trip();
    test_empty_and_max_payload();
    test_sync_byte_inside_payload();
    test_crc_catches_every_single_bit_flip();
    test_truncated_frame_then_recovery();
    test_garbage_before_a_frame();
    test_false_sync_eats_one_frame_then_recovers();
    test_encode_refuses_a_short_buffer();
    test_counters();
    test_hello_round_trip();
    test_hello_rejects_wrong_shape();
    test_log_event();
    test_unset_clock_round_trips();
    test_decode_event();
    test_queue_entry_event();
    test_slot_state_event();
    test_tx_hud_event();
    test_action_set_clock();
    test_action_tx_free_and_cancel();
    test_action_connect_and_tune();
    test_action_beacon();
    test_action_queue_cancel_and_reply();
    test_ack_and_nak();
    test_config_kv();
    test_file_list_and_read_requests();
    test_file_data_rows();

    if (g_fail) {
        std::printf("FAILED: %d check(s)\n", g_fail);
        return 1;
    }
    std::printf("PASS: porta protocol codec\n");
    return 0;
}
