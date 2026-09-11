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

bool porta_proto_decode_hello(const porta_frame_t *f, uint8_t *protocol_version_out,
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
