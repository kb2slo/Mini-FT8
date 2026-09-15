#include "porta_proto.h"

#include <string.h>

enum {
    ST_IDLE = 0,   // hunting for the sync byte
    ST_TYPE,
    ST_LEN,
    ST_PAYLOAD,
    ST_CRC,
};

uint8_t porta_proto_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

size_t porta_proto_encode(uint8_t type, const uint8_t *payload, uint8_t len,
                          uint8_t *out, size_t out_cap)
{
    const size_t need = 1u + 1u + 1u + (size_t)len + 1u;
    if (!out || out_cap < need) {
        return 0;  // never a partial frame
    }
    if (len > 0 && !payload) {
        return 0;
    }

    out[0] = PORTA_PROTO_SYNC;
    out[1] = type;
    out[2] = len;
    if (len > 0) {
        memcpy(&out[3], payload, len);
    }
    // Covers type, len and payload -- not sync. See the header for why.
    out[3 + len] = porta_proto_crc8(&out[1], (size_t)len + 2u);
    return need;
}

void porta_decoder_init(porta_decoder_t *d)
{
    if (!d) {
        return;
    }
    memset(d, 0, sizeof(*d));
    d->state = ST_IDLE;
}

bool porta_decoder_push(porta_decoder_t *d, uint8_t byte, porta_frame_t *out)
{
    if (!d || !out) {
        return false;
    }

    switch (d->state) {
    case ST_IDLE:
        // Sync is only meaningful here. Once a length is known the decoder
        // counts bytes instead of hunting, so a 0xC6 inside a payload is data.
        if (byte == PORTA_PROTO_SYNC) {
            d->state = ST_TYPE;
        }
        return false;

    case ST_TYPE:
        d->type = byte;
        d->state = ST_LEN;
        return false;

    case ST_LEN:
        d->len = byte;
        d->got = 0;
        d->state = (byte == 0) ? ST_CRC : ST_PAYLOAD;
        return false;

    case ST_PAYLOAD:
        d->payload[d->got++] = byte;
        if (d->got >= d->len) {
            d->state = ST_CRC;
        }
        return false;

    case ST_CRC: {
        uint8_t header[2];
        header[0] = d->type;
        header[1] = d->len;
        uint8_t crc = porta_proto_crc8(header, 2);
        // Continue the same CRC across the payload rather than copying the
        // frame into one contiguous buffer just to checksum it.
        for (uint16_t i = 0; i < d->len; ++i) {
            crc ^= d->payload[i];
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
            }
        }

        const bool ok = (crc == byte);
        if (ok) {
            out->type = d->type;
            out->len = d->len;
            if (d->len > 0) {
                memcpy(out->payload, d->payload, d->len);
            }
            d->frames_ok++;
        } else {
            d->crc_errors++;
        }
        // Either way we are done with this frame. A bad one is dropped without
        // rescanning the bytes it consumed -- see the header for what that
        // costs and why it is worth it.
        d->state = ST_IDLE;
        d->got = 0;
        return ok;
    }

    default:
        d->state = ST_IDLE;
        return false;
    }
}

size_t porta_proto_encode_hello(uint8_t protocol_version, const char *build_version,
                                uint8_t *out, size_t out_cap)
{
    uint8_t payload[PORTA_HELLO_PAYLOAD_LEN];
    memset(payload, 0, sizeof(payload));
    payload[0] = protocol_version;
    if (build_version) {
        // strncpy semantics without the terminator guarantee: a version that
        // fills all 32 bytes is legal and is not truncated to make room for a
        // NUL that the wire format does not carry.
        size_t n = strnlen(build_version, PORTA_HELLO_VERSION_LEN);
        memcpy(&payload[1], build_version, n);
    }
    return porta_proto_encode(PORTA_MSG_HELLO, payload, (uint8_t)sizeof(payload), out, out_cap);
}

bool porta_proto_parse_hello(const porta_frame_t *f, uint8_t *protocol_version_out,
                              char *build_version_out)
{
    if (!f || f->type != PORTA_MSG_HELLO || f->len != PORTA_HELLO_PAYLOAD_LEN) {
        return false;
    }
    if (protocol_version_out) {
        *protocol_version_out = f->payload[0];
    }
    if (build_version_out) {
        memcpy(build_version_out, &f->payload[1], PORTA_HELLO_VERSION_LEN);
        build_version_out[PORTA_HELLO_VERSION_LEN] = '\0';
    }
    return true;
}

// --- EVENT payloads ------------------------------------------------------

// Shared shape: payload[0] is the subtype, and for text-bearing events the
// remainder is the text. Callers get a NUL-terminated copy even though the wire
// carries none -- the frame length is the terminator.
// subtype(1) + epoch_secs(4), little-endian, shared by every event subtype.
#define EVENT_HEADER_LEN 5u

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static size_t event_text_out(const porta_frame_t *f, size_t body_offset,
                             char *out, size_t out_cap)
{
    if (f->len < body_offset) {
        return 0;
    }
    size_t n = (size_t)f->len - body_offset;
    if (n > out_cap - 1) {
        n = out_cap - 1;
    }
    memcpy(out, &f->payload[body_offset], n);
    out[n] = '\0';
    return n;
}

size_t porta_proto_encode_log(uint32_t epoch_secs, const char *text,
                              uint8_t *out, size_t out_cap)
{
    uint8_t payload[EVENT_HEADER_LEN + PORTA_EVENT_TEXT_MAX];
    payload[0] = PORTA_EVT_LOG;
    put_u32(&payload[1], epoch_secs);
    size_t n = text ? strnlen(text, PORTA_EVENT_TEXT_MAX) : 0;
    if (n > 0) {
        memcpy(&payload[EVENT_HEADER_LEN], text, n);
    }
    return porta_proto_encode(PORTA_MSG_EVENT, payload,
                              (uint8_t)(EVENT_HEADER_LEN + n), out, out_cap);
}

bool porta_proto_parse_log(const porta_frame_t *f, uint32_t *epoch_secs_out,
                           char *text_out)
{
    if (!f || !text_out || f->type != PORTA_MSG_EVENT || f->len < EVENT_HEADER_LEN ||
        f->payload[0] != PORTA_EVT_LOG) {
        return false;
    }
    if (epoch_secs_out) {
        *epoch_secs_out = get_u32(&f->payload[1]);
    }
    event_text_out(f, EVENT_HEADER_LEN, text_out, PORTA_EVENT_TEXT_MAX + 1);
    return true;
}

// subtype | epoch(4) | flags | snr | offset(2) | dt(2) | decode_id(4) | text...
#define DECODE_BODY_OFFSET (EVENT_HEADER_LEN + 6u + 4u)

size_t porta_proto_encode_decode(const porta_decode_event_t *d, uint8_t *out, size_t out_cap)
{
    if (!d) {
        return 0;
    }
    uint8_t payload[DECODE_BODY_OFFSET + PORTA_EVENT_TEXT_MAX];
    payload[0] = PORTA_EVT_DECODE;
    put_u32(&payload[1], d->epoch_secs);
    payload[5] = (uint8_t)((d->is_cq ? 0x01u : 0u) |
                           (d->is_to_me ? 0x02u : 0u) |
                           (d->is_recent_qso ? 0x04u : 0u));
    payload[6] = (uint8_t)d->snr;
    payload[7] = (uint8_t)(d->offset_hz & 0xFFu);
    payload[8] = (uint8_t)(d->offset_hz >> 8);
    payload[9] = (uint8_t)((uint16_t)d->dt_centis & 0xFFu);
    payload[10] = (uint8_t)((uint16_t)d->dt_centis >> 8);
    put_u32(&payload[11], d->decode_id);

    size_t n = strnlen(d->text, PORTA_EVENT_TEXT_MAX);
    if (n > 0) {
        memcpy(&payload[DECODE_BODY_OFFSET], d->text, n);
    }
    return porta_proto_encode(PORTA_MSG_EVENT, payload,
                              (uint8_t)(DECODE_BODY_OFFSET + n), out, out_cap);
}

bool porta_proto_parse_decode(const porta_frame_t *f, porta_decode_event_t *out)
{
    if (!f || !out || f->type != PORTA_MSG_EVENT || f->len < DECODE_BODY_OFFSET ||
        f->payload[0] != PORTA_EVT_DECODE) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->epoch_secs    = get_u32(&f->payload[1]);
    out->is_cq         = (f->payload[5] & 0x01u) != 0;
    out->is_to_me      = (f->payload[5] & 0x02u) != 0;
    out->is_recent_qso = (f->payload[5] & 0x04u) != 0;
    out->snr           = (int8_t)f->payload[6];
    out->offset_hz     = (uint16_t)(f->payload[7] | ((uint16_t)f->payload[8] << 8));
    out->dt_centis     = (int16_t)(uint16_t)(f->payload[9] | ((uint16_t)f->payload[10] << 8));
    out->decode_id     = get_u32(&f->payload[11]);
    event_text_out(f, DECODE_BODY_OFFSET, out->text, sizeof(out->text));
    return true;
}

// subtype | epoch(4) | entry_id(2) | state | retry | retry_limit | dxcall_len | dxcall...
#define QUEUE_ENTRY_FIXED_LEN (EVENT_HEADER_LEN + 2u + 1u + 1u + 1u + 1u)

size_t porta_proto_encode_queue_entry(const porta_queue_entry_event_t *q,
                                      uint8_t *out, size_t out_cap)
{
    if (!q) {
        return 0;
    }
    const size_t call_n = strnlen(q->dxcall, PORTA_CALLSIGN_MAX + 1);
    if (call_n > PORTA_CALLSIGN_MAX) {
        return 0;
    }
    uint8_t payload[QUEUE_ENTRY_FIXED_LEN + PORTA_CALLSIGN_MAX];
    payload[0] = PORTA_EVT_QUEUE_ENTRY;
    put_u32(&payload[1], q->epoch_secs);
    put_u16(&payload[5], q->entry_id);
    payload[7] = q->state;
    payload[8] = q->retry_count;
    payload[9] = q->retry_limit;
    payload[10] = (uint8_t)call_n;
    if (call_n > 0) {
        memcpy(&payload[QUEUE_ENTRY_FIXED_LEN], q->dxcall, call_n);
    }
    return porta_proto_encode(PORTA_MSG_EVENT, payload,
                              (uint8_t)(QUEUE_ENTRY_FIXED_LEN + call_n), out, out_cap);
}

bool porta_proto_parse_queue_entry(const porta_frame_t *f, porta_queue_entry_event_t *out)
{
    if (!f || !out || f->type != PORTA_MSG_EVENT || f->len < QUEUE_ENTRY_FIXED_LEN ||
        f->payload[0] != PORTA_EVT_QUEUE_ENTRY) {
        return false;
    }
    const uint8_t call_n = f->payload[10];
    if ((size_t)QUEUE_ENTRY_FIXED_LEN + call_n > f->len || call_n > PORTA_CALLSIGN_MAX) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->epoch_secs   = get_u32(&f->payload[1]);
    out->entry_id     = get_u16(&f->payload[5]);
    out->state        = f->payload[7];
    out->retry_count  = f->payload[8];
    out->retry_limit  = f->payload[9];
    memcpy(out->dxcall, &f->payload[QUEUE_ENTRY_FIXED_LEN], call_n);
    out->dxcall[call_n] = '\0';
    return true;
}

// subtype | epoch(4) | slot_parity | beacon_mode | resolved_offset_hz(2)
#define SLOT_STATE_LEN (EVENT_HEADER_LEN + 1u + 1u + 2u)

size_t porta_proto_encode_slot_state(const porta_slot_state_event_t *s,
                                     uint8_t *out, size_t out_cap)
{
    if (!s) {
        return 0;
    }
    uint8_t payload[SLOT_STATE_LEN];
    payload[0] = PORTA_EVT_SLOT_STATE;
    put_u32(&payload[1], s->epoch_secs);
    payload[5] = s->slot_parity;
    payload[6] = s->beacon_mode;
    put_u16(&payload[7], s->resolved_offset_hz);
    return porta_proto_encode(PORTA_MSG_EVENT, payload, sizeof(payload), out, out_cap);
}

bool porta_proto_parse_slot_state(const porta_frame_t *f, porta_slot_state_event_t *out)
{
    if (!f || !out || f->type != PORTA_MSG_EVENT || f->len != SLOT_STATE_LEN ||
        f->payload[0] != PORTA_EVT_SLOT_STATE) {
        return false;
    }
    out->epoch_secs        = get_u32(&f->payload[1]);
    out->slot_parity       = f->payload[5];
    out->beacon_mode       = f->payload[6];
    out->resolved_offset_hz = get_u16(&f->payload[7]);
    return true;
}

// --- ACTION payloads -----------------------------------------------------

size_t porta_proto_encode_set_clock(uint32_t epoch_secs, uint16_t millis,
                                    uint8_t *out, size_t out_cap)
{
    uint8_t payload[7];
    payload[0] = PORTA_ACT_SET_CLOCK;
    put_u32(&payload[1], epoch_secs);
    payload[5] = (uint8_t)(millis & 0xFFu);
    payload[6] = (uint8_t)(millis >> 8);
    return porta_proto_encode(PORTA_MSG_ACTION, payload, sizeof(payload), out, out_cap);
}

bool porta_proto_parse_set_clock(const porta_frame_t *f, uint32_t *epoch_secs_out,
                                 uint16_t *millis_out)
{
    if (!f || f->type != PORTA_MSG_ACTION || f->len != 7 ||
        f->payload[0] != PORTA_ACT_SET_CLOCK) {
        return false;
    }
    if (epoch_secs_out) {
        *epoch_secs_out = get_u32(&f->payload[1]);
    }
    if (millis_out) {
        *millis_out = (uint16_t)(f->payload[5] | ((uint16_t)f->payload[6] << 8));
    }
    return true;
}

size_t porta_proto_encode_tx_free(const char *text, uint8_t *out, size_t out_cap)
{
    if (!text || text[0] == '\0') {
        return 0;
    }
    uint8_t payload[1 + PORTA_EVENT_TEXT_MAX];
    payload[0] = PORTA_ACT_TX_FREE;
    size_t n = strnlen(text, PORTA_EVENT_TEXT_MAX);
    memcpy(&payload[1], text, n);
    return porta_proto_encode(PORTA_MSG_ACTION, payload, (uint8_t)(1 + n), out, out_cap);
}

bool porta_proto_parse_tx_free(const porta_frame_t *f, char *text_out)
{
    if (!f || !text_out || f->type != PORTA_MSG_ACTION || f->len < 2 ||
        f->payload[0] != PORTA_ACT_TX_FREE) {
        return false;
    }
    event_text_out(f, 1, text_out, PORTA_EVENT_TEXT_MAX + 1);
    return text_out[0] != '\0';
}

size_t porta_proto_encode_tx_cancel(uint8_t *out, size_t out_cap)
{
    const uint8_t verb = PORTA_ACT_TX_CANCEL;
    return porta_proto_encode(PORTA_MSG_ACTION, &verb, 1, out, out_cap);
}

bool porta_proto_parse_tx_cancel(const porta_frame_t *f)
{
    return f && f->type == PORTA_MSG_ACTION && f->len == 1 &&
           f->payload[0] == PORTA_ACT_TX_CANCEL;
}

size_t porta_proto_encode_connect(uint8_t *out, size_t out_cap)
{
    const uint8_t verb = PORTA_ACT_CONNECT;
    return porta_proto_encode(PORTA_MSG_ACTION, &verb, 1, out, out_cap);
}

bool porta_proto_parse_connect(const porta_frame_t *f)
{
    return f && f->type == PORTA_MSG_ACTION && f->len == 1 &&
           f->payload[0] == PORTA_ACT_CONNECT;
}

size_t porta_proto_encode_tune(bool on, uint8_t *out, size_t out_cap)
{
    uint8_t payload[2] = {PORTA_ACT_TUNE, on ? 1u : 0u};
    return porta_proto_encode(PORTA_MSG_ACTION, payload, 2, out, out_cap);
}

bool porta_proto_parse_tune(const porta_frame_t *f, bool *on_out)
{
    if (!f || f->type != PORTA_MSG_ACTION || f->len != 2 ||
        f->payload[0] != PORTA_ACT_TUNE) {
        return false;
    }
    if (on_out) {
        *on_out = (f->payload[1] != 0);
    }
    return true;
}

size_t porta_proto_encode_beacon(uint8_t mode, uint8_t *out, size_t out_cap)
{
    uint8_t payload[2] = {PORTA_ACT_BEACON, mode};
    return porta_proto_encode(PORTA_MSG_ACTION, payload, 2, out, out_cap);
}

bool porta_proto_parse_beacon(const porta_frame_t *f, uint8_t *mode_out)
{
    if (!f || f->type != PORTA_MSG_ACTION || f->len != 2 ||
        f->payload[0] != PORTA_ACT_BEACON) {
        return false;
    }
    if (mode_out) {
        *mode_out = f->payload[1];
    }
    return true;
}

size_t porta_proto_encode_queue_cancel(uint16_t entry_id, uint8_t *out, size_t out_cap)
{
    uint8_t payload[3];
    payload[0] = PORTA_ACT_QUEUE_CANCEL;
    put_u16(&payload[1], entry_id);
    return porta_proto_encode(PORTA_MSG_ACTION, payload, sizeof(payload), out, out_cap);
}

bool porta_proto_parse_queue_cancel(const porta_frame_t *f, uint16_t *entry_id_out)
{
    if (!f || f->type != PORTA_MSG_ACTION || f->len != 3 ||
        f->payload[0] != PORTA_ACT_QUEUE_CANCEL) {
        return false;
    }
    if (entry_id_out) {
        *entry_id_out = get_u16(&f->payload[1]);
    }
    return true;
}

size_t porta_proto_encode_queue_reply(uint32_t decode_id, uint8_t *out, size_t out_cap)
{
    uint8_t payload[5];
    payload[0] = PORTA_ACT_QUEUE_REPLY;
    put_u32(&payload[1], decode_id);
    return porta_proto_encode(PORTA_MSG_ACTION, payload, sizeof(payload), out, out_cap);
}

bool porta_proto_parse_queue_reply(const porta_frame_t *f, uint32_t *decode_id_out)
{
    if (!f || f->type != PORTA_MSG_ACTION || f->len != 5 ||
        f->payload[0] != PORTA_ACT_QUEUE_REPLY) {
        return false;
    }
    if (decode_id_out) {
        *decode_id_out = get_u32(&f->payload[1]);
    }
    return true;
}

size_t porta_proto_encode_ack(uint8_t verb, uint8_t *out, size_t out_cap)
{
    return porta_proto_encode(PORTA_MSG_ACK, &verb, 1, out, out_cap);
}

size_t porta_proto_encode_nak(uint8_t verb, const char *reason,
                              uint8_t *out, size_t out_cap)
{
    uint8_t payload[1 + PORTA_EVENT_TEXT_MAX];
    payload[0] = verb;
    size_t n = reason ? strnlen(reason, PORTA_EVENT_TEXT_MAX) : 0;
    if (n > 0) {
        memcpy(&payload[1], reason, n);
    }
    return porta_proto_encode(PORTA_MSG_NAK, payload, (uint8_t)(1 + n), out, out_cap);
}

bool porta_proto_parse_ack(const porta_frame_t *f, uint8_t *verb_out)
{
    if (!f || f->type != PORTA_MSG_ACK || f->len != 1) {
        return false;
    }
    if (verb_out) {
        *verb_out = f->payload[0];
    }
    return true;
}

bool porta_proto_parse_nak(const porta_frame_t *f, uint8_t *verb_out, char *reason_out)
{
    if (!f || f->type != PORTA_MSG_NAK || f->len < 1) {
        return false;
    }
    if (verb_out) {
        *verb_out = f->payload[0];
    }
    if (reason_out) {
        event_text_out(f, 1, reason_out, PORTA_EVENT_TEXT_MAX + 1);
    }
    return true;
}

// --- CONFIG payloads -----------------------------------------------------

static size_t encode_config_kv(uint8_t type, const char *key, const char *value,
                               bool require_key, uint8_t *out, size_t out_cap)
{
    const size_t key_n = key ? strnlen(key, PORTA_CONFIG_KEY_MAX + 1) : 0;
    if (key_n > PORTA_CONFIG_KEY_MAX) {
        return 0;
    }
    if (require_key && key_n == 0) {
        return 0;
    }
    const size_t val_n = value ? strnlen(value, PORTA_CONFIG_VALUE_MAX + 1) : 0;
    if (val_n > PORTA_CONFIG_VALUE_MAX) {
        return 0;
    }
    if (1u + key_n + val_n > PORTA_PROTO_MAX_PAYLOAD) {
        return 0;
    }
    uint8_t payload[PORTA_PROTO_MAX_PAYLOAD];
    payload[0] = (uint8_t)key_n;
    if (key_n > 0) {
        memcpy(&payload[1], key, key_n);
    }
    if (val_n > 0) {
        memcpy(&payload[1 + key_n], value, val_n);
    }
    return porta_proto_encode(type, payload, (uint8_t)(1u + key_n + val_n), out, out_cap);
}

static bool parse_config_kv(const porta_frame_t *f, uint8_t type, bool require_key,
                            char *key_out, char *value_out)
{
    if (!f || f->type != type || f->len < 1 || !key_out) {
        return false;
    }
    const uint8_t key_n = f->payload[0];
    if ((size_t)key_n + 1u > f->len || key_n > PORTA_CONFIG_KEY_MAX) {
        return false;
    }
    if (require_key && key_n == 0) {
        return false;
    }
    const size_t val_n = (size_t)f->len - 1u - (size_t)key_n;
    if (val_n > PORTA_CONFIG_VALUE_MAX) {
        return false;
    }
    memcpy(key_out, &f->payload[1], key_n);
    key_out[key_n] = '\0';
    if (value_out) {
        memcpy(value_out, &f->payload[1 + key_n], val_n);
        value_out[val_n] = '\0';
    }
    return true;
}

size_t porta_proto_encode_config_get(const char *key, uint8_t *out, size_t out_cap)
{
    // GET carries no value; empty key means get-all.
    return encode_config_kv(PORTA_MSG_CONFIG_GET, key, NULL, false, out, out_cap);
}

size_t porta_proto_encode_config_set(const char *key, const char *value,
                                     uint8_t *out, size_t out_cap)
{
    if (!value) {
        return 0;
    }
    return encode_config_kv(PORTA_MSG_CONFIG_SET, key, value, true, out, out_cap);
}

size_t porta_proto_encode_config_value(const char *key, const char *value,
                                       uint8_t *out, size_t out_cap)
{
    if (!value) {
        return 0;
    }
    return encode_config_kv(PORTA_MSG_CONFIG_VALUE, key, value, true, out, out_cap);
}

bool porta_proto_parse_config_get(const porta_frame_t *f, char *key_out)
{
    return parse_config_kv(f, PORTA_MSG_CONFIG_GET, false, key_out, NULL);
}

bool porta_proto_parse_config_set(const porta_frame_t *f, char *key_out, char *value_out)
{
    return parse_config_kv(f, PORTA_MSG_CONFIG_SET, true, key_out, value_out);
}

bool porta_proto_parse_config_value(const porta_frame_t *f, char *key_out, char *value_out)
{
    return parse_config_kv(f, PORTA_MSG_CONFIG_VALUE, true, key_out, value_out);
}

// --- FILE_LIST / FILE_READ / FILE_DATA payloads ---------------------------

// kind(1) | skip(2) | take(1)
size_t porta_proto_encode_file_list_req(uint8_t kind, uint16_t skip, uint8_t take,
                                        uint8_t *out, size_t out_cap)
{
    uint8_t payload[4];
    payload[0] = kind;
    put_u16(&payload[1], skip);
    payload[3] = take;
    return porta_proto_encode(PORTA_MSG_FILE_LIST, payload, sizeof(payload), out, out_cap);
}

bool porta_proto_parse_file_list_req(const porta_frame_t *f, uint8_t *kind_out,
                                     uint16_t *skip_out, uint8_t *take_out)
{
    if (!f || f->type != PORTA_MSG_FILE_LIST || f->len != 4) {
        return false;
    }
    if (kind_out) {
        *kind_out = f->payload[0];
    }
    if (skip_out) {
        *skip_out = get_u16(&f->payload[1]);
    }
    if (take_out) {
        *take_out = f->payload[3];
    }
    return true;
}

// skip(2) | take(1) | filename_len(1) | filename...
size_t porta_proto_encode_file_read_req(const char *filename, uint16_t skip, uint8_t take,
                                        uint8_t *out, size_t out_cap)
{
    const size_t name_n = filename ? strnlen(filename, PORTA_FILENAME_MAX + 1) : 0;
    if (name_n == 0 || name_n > PORTA_FILENAME_MAX) {
        return 0;
    }
    uint8_t payload[4 + PORTA_FILENAME_MAX];
    put_u16(&payload[0], skip);
    payload[2] = take;
    payload[3] = (uint8_t)name_n;
    memcpy(&payload[4], filename, name_n);
    return porta_proto_encode(PORTA_MSG_FILE_READ, payload, (uint8_t)(4 + name_n), out, out_cap);
}

bool porta_proto_parse_file_read_req(const porta_frame_t *f, char *filename_out,
                                     uint16_t *skip_out, uint8_t *take_out)
{
    if (!f || !filename_out || f->type != PORTA_MSG_FILE_READ || f->len < 4) {
        return false;
    }
    const uint8_t name_n = f->payload[3];
    if ((size_t)4 + name_n != f->len || name_n == 0 || name_n > PORTA_FILENAME_MAX) {
        return false;
    }
    if (skip_out) {
        *skip_out = get_u16(&f->payload[0]);
    }
    if (take_out) {
        *take_out = f->payload[2];
    }
    memcpy(filename_out, &f->payload[4], name_n);
    filename_out[name_n] = '\0';
    return true;
}

// data_kind(1)=NAME | name_len(1) | name...
size_t porta_proto_encode_file_name_row(const char *name, uint8_t *out, size_t out_cap)
{
    const size_t name_n = name ? strnlen(name, PORTA_FILENAME_MAX + 1) : 0;
    if (name_n == 0 || name_n > PORTA_FILENAME_MAX) {
        return 0;
    }
    uint8_t payload[2 + PORTA_FILENAME_MAX];
    payload[0] = PORTA_FILE_DATA_NAME;
    payload[1] = (uint8_t)name_n;
    memcpy(&payload[2], name, name_n);
    return porta_proto_encode(PORTA_MSG_FILE_DATA, payload, (uint8_t)(2 + name_n), out, out_cap);
}

bool porta_proto_parse_file_name_row(const porta_frame_t *f, char *name_out)
{
    if (!f || !name_out || f->type != PORTA_MSG_FILE_DATA || f->len < 2 ||
        f->payload[0] != PORTA_FILE_DATA_NAME) {
        return false;
    }
    const uint8_t name_n = f->payload[1];
    if ((size_t)2 + name_n != f->len || name_n == 0 || name_n > PORTA_FILENAME_MAX) {
        return false;
    }
    memcpy(name_out, &f->payload[2], name_n);
    name_out[name_n] = '\0';
    return true;
}

// data_kind(1)=ENTRY | time_on(5, fixed "HH:MM") | band_len(1) | band... |
// call_len(1) | call... | rst_sent(1) | rst_rcvd(1)
size_t porta_proto_encode_file_entry_row(const porta_qso_entry_row_t *e,
                                         uint8_t *out, size_t out_cap)
{
    if (!e) {
        return 0;
    }
    const size_t time_n = strnlen(e->time_on, 6);
    if (time_n != 5) {
        return 0;  // qso_browse.cpp always yields exactly "HH:MM" or "??:??"
    }
    const size_t band_n = strnlen(e->band, PORTA_BAND_MAX + 1);
    const size_t call_n = strnlen(e->call, PORTA_CALLSIGN_MAX + 1);
    if (band_n > PORTA_BAND_MAX || call_n > PORTA_CALLSIGN_MAX) {
        return 0;
    }
    uint8_t payload[1 + 5 + 1 + PORTA_BAND_MAX + 1 + PORTA_CALLSIGN_MAX + 1 + 1];
    size_t p = 0;
    payload[p++] = PORTA_FILE_DATA_ENTRY;
    memcpy(&payload[p], e->time_on, 5);
    p += 5;
    payload[p++] = (uint8_t)band_n;
    memcpy(&payload[p], e->band, band_n);
    p += band_n;
    payload[p++] = (uint8_t)call_n;
    memcpy(&payload[p], e->call, call_n);
    p += call_n;
    payload[p++] = (uint8_t)e->rst_sent;
    payload[p++] = (uint8_t)e->rst_rcvd;
    return porta_proto_encode(PORTA_MSG_FILE_DATA, payload, (uint8_t)p, out, out_cap);
}

bool porta_proto_parse_file_entry_row(const porta_frame_t *f, porta_qso_entry_row_t *out)
{
    if (!f || !out || f->type != PORTA_MSG_FILE_DATA || f->len < 1 + 5 + 1 + 1 + 1 + 1 ||
        f->payload[0] != PORTA_FILE_DATA_ENTRY) {
        return false;
    }
    size_t p = 1;
    memset(out, 0, sizeof(*out));
    memcpy(out->time_on, &f->payload[p], 5);
    out->time_on[5] = '\0';
    p += 5;

    const uint8_t band_n = f->payload[p++];
    if (band_n > PORTA_BAND_MAX || p + band_n + 1 > f->len) {
        return false;
    }
    memcpy(out->band, &f->payload[p], band_n);
    out->band[band_n] = '\0';
    p += band_n;

    const uint8_t call_n = f->payload[p++];
    if (call_n > PORTA_CALLSIGN_MAX || p + call_n + 2 != f->len) {
        return false;
    }
    memcpy(out->call, &f->payload[p], call_n);
    out->call[call_n] = '\0';
    p += call_n;

    out->rst_sent = (int8_t)f->payload[p++];
    out->rst_rcvd = (int8_t)f->payload[p++];
    return true;
}
