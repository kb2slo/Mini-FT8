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

size_t porta_proto_encode_log(const char *text, uint8_t *out, size_t out_cap)
{
    uint8_t payload[1 + PORTA_EVENT_TEXT_MAX];
    payload[0] = PORTA_EVT_LOG;
    size_t n = text ? strnlen(text, PORTA_EVENT_TEXT_MAX) : 0;
    if (n > 0) {
        memcpy(&payload[1], text, n);
    }
    return porta_proto_encode(PORTA_MSG_EVENT, payload, (uint8_t)(1 + n), out, out_cap);
}

bool porta_proto_parse_log(const porta_frame_t *f, char *text_out)
{
    if (!f || !text_out || f->type != PORTA_MSG_EVENT || f->len < 1 ||
        f->payload[0] != PORTA_EVT_LOG) {
        return false;
    }
    event_text_out(f, 1, text_out, PORTA_EVENT_TEXT_MAX + 1);
    return true;
}

// subtype | flags | snr | offset_lo | offset_hi | dt_lo | dt_hi | text...
#define DECODE_BODY_OFFSET 7u

size_t porta_proto_encode_decode(const porta_decode_event_t *d, uint8_t *out, size_t out_cap)
{
    if (!d) {
        return 0;
    }
    uint8_t payload[DECODE_BODY_OFFSET + PORTA_EVENT_TEXT_MAX];
    payload[0] = PORTA_EVT_DECODE;
    payload[1] = (uint8_t)((d->is_cq ? 0x01u : 0u) |
                           (d->is_to_me ? 0x02u : 0u) |
                           (d->is_recent_qso ? 0x04u : 0u));
    payload[2] = (uint8_t)d->snr;
    payload[3] = (uint8_t)(d->offset_hz & 0xFFu);
    payload[4] = (uint8_t)(d->offset_hz >> 8);
    payload[5] = (uint8_t)((uint16_t)d->dt_centis & 0xFFu);
    payload[6] = (uint8_t)((uint16_t)d->dt_centis >> 8);

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
    out->is_cq         = (f->payload[1] & 0x01u) != 0;
    out->is_to_me      = (f->payload[1] & 0x02u) != 0;
    out->is_recent_qso = (f->payload[1] & 0x04u) != 0;
    out->snr           = (int8_t)f->payload[2];
    out->offset_hz     = (uint16_t)(f->payload[3] | ((uint16_t)f->payload[4] << 8));
    out->dt_centis     = (int16_t)(uint16_t)(f->payload[5] | ((uint16_t)f->payload[6] << 8));
    event_text_out(f, DECODE_BODY_OFFSET, out->text, sizeof(out->text));
    return true;
}
