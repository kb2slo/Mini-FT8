#pragma once

// ============================================================================
// porta_proto.h — framing for the sidekick/main link (I28a, RFC 0004 §6).
//
// Pure codec: no UART, no I2C, no globals, no ESP-IDF. Host-tested
// (host_test_porta_proto). It lives in components/ rather than main/ because
// two idf.py projects compile it -- the ADV and the sidekick -- which is the
// named exception in RFC 0002 §5.1.
//
// WIRE FORMAT
//
//     0xC6 | type | len | payload[len] | crc8
//
// `0xC6` is the existing companion sync byte and stays for a reason that is
// not aesthetic: main/porta.cpp decides whether a GPS or a sidekick is on
// Port A purely by listening, and 0xC6 was chosen because it cannot begin an
// NMEA sentence. Changing it would break role arbitration.
//
// The CRC covers type, len and payload -- NOT the sync byte. Sync is a
// constant, so it contributes no detection power, and excluding it means the
// checksummed bytes are exactly the ones that vary. That matters for B47: on a
// transport where framing is inherent (an I2C transaction has its own start
// and length) the sync byte is redundant and can be dropped without touching
// the CRC.
//
// WHY IT LOOKS LIKE THIS
//
// One message per frame, length-prefixed, max 255 bytes of payload. Frames
// stay small deliberately: RFC 0004 §6 requires framing that does not assume a
// stream, because B47 leaves UART-versus-I2C open, and small self-describing
// frames map onto a single I2C transaction as readily as onto a byte stream.
// A busy FT8 slot produces more decodes than fit in one frame; the polled
// design (also RFC 0004 §6) handles that by the poller asking again, rather
// than by growing the frame.
//
// RESYNC, AND WHAT IT COSTS
//
// The decoder only looks for sync while idle. Once it has type and len it
// consumes exactly that many bytes, so a 0xC6 *inside* a payload is data and
// not a frame start -- which matters, because payload bytes are binary and
// 0xC6 will occur.
//
// A corrupt frame is dropped and the decoder returns to idle. It does not
// rescan the bytes it consumed. That is a deliberate simplification with a
// stated cost: if line noise happens to contain 0xC6 and is immediately
// followed by a real frame, that real frame is lost too. Recovery is the next
// frame -- at worst one poll interval, or one second for the beacon. Rescanning
// would recover it at the price of a buffer and a replay path, which is not
// worth it on a two-wire link between boards in the same enclosure.
// ============================================================================

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORTA_PROTO_SYNC 0xC6u

// Bumped only when a change breaks an older peer. RFC 0004 §4's version
// pairing applies here too: this is NOT the git build version the beacon
// carries -- they answer different questions, and conflating them is how a
// cosmetic rebuild reads as a protocol change.
#define PORTA_PROTO_VERSION 1u

#define PORTA_PROTO_MAX_PAYLOAD 255u
#define PORTA_PROTO_MAX_FRAME (1u + 1u + 1u + PORTA_PROTO_MAX_PAYLOAD + 1u)  // 259

// v1 message types, derived by walking the host's existing UI surface rather
// than sketched: ~16 of its affordances are config values, ~13 are actions, and
// the rest are live views or paged bulk data. Three of these are *namespaces*
// addressed by an inner byte -- CONFIG by key, ACTION by verb, EVENT by subtype
// -- so new settings, verbs and telemetry are added inside a namespace and this
// list stops growing. That is why it is short and why it should stay short.
typedef enum {
    PORTA_MSG_HELLO        = 0x01,  // either end: protocol version + build version
    PORTA_MSG_POLL         = 0x02,  // sidekick -> host: anything pending?
    PORTA_MSG_NOTHING      = 0x03,  // host -> sidekick: nothing pending
    PORTA_MSG_DESCRIBE     = 0x04,  // sidekick -> host: describe yourself
    PORTA_MSG_MANIFEST     = 0x05,  // host -> sidekick: config metadata, actions, events
    PORTA_MSG_CONFIG_GET   = 0x06,
    PORTA_MSG_CONFIG_SET   = 0x07,
    PORTA_MSG_CONFIG_VALUE = 0x08,
    PORTA_MSG_ACTION       = 0x09,  // namespace: verb in the payload
    PORTA_MSG_EVENT        = 0x0A,  // namespace: subtype in the payload
    PORTA_MSG_FILE_LIST    = 0x0B,
    PORTA_MSG_FILE_READ    = 0x0C,
    PORTA_MSG_FILE_DATA    = 0x0D,
    PORTA_MSG_ACK          = 0x0E,
    PORTA_MSG_NAK          = 0x0F,  // payload carries the reason
    // Firmware push toward the host (web app -> sidekick -> host). Separate
    // from the FILE_* messages on purpose: the failure semantics are different,
    // a 2 MB image takes minutes on this link, and a transfer that dies partway
    // must resume rather than restart.
    PORTA_MSG_FW_BEGIN     = 0x10,
    PORTA_MSG_FW_CHUNK     = 0x11,
    PORTA_MSG_FW_END       = 0x12,
} porta_msg_type_t;

typedef struct {
    uint8_t type;
    uint8_t len;
    uint8_t payload[PORTA_PROTO_MAX_PAYLOAD];
} porta_frame_t;

// CRC-8, polynomial 0x07 (x^8 + x^2 + x + 1), init 0x00, no reflection.
// Bitwise rather than table-driven: 259 bytes at 1 Hz does not justify 256
// bytes of table on either part.
uint8_t porta_proto_crc8(const uint8_t *data, size_t len);

// Encodes one frame into `out`. Returns bytes written, or 0 if `out_cap` is
// too small -- never a partial frame, so a short buffer cannot put half a
// message on the wire.
size_t porta_proto_encode(uint8_t type, const uint8_t *payload, uint8_t len,
                          uint8_t *out, size_t out_cap);

// Byte-at-a-time decoder. Allocation-free, and identical for a UART stream and
// an I2C transaction replayed through it, which is what keeps the transport
// swappable (B47).
typedef struct {
    uint8_t  state;
    uint8_t  type;
    uint8_t  len;
    uint16_t got;      // payload bytes received so far
    uint8_t  payload[PORTA_PROTO_MAX_PAYLOAD];
    uint32_t frames_ok;
    uint32_t crc_errors;
} porta_decoder_t;

// Zeroes everything, counters included. There is no separate "resync" call:
// the decoder returns itself to idle after a bad frame without losing its
// counters, so the only reason to call this is to start a new link.
void porta_decoder_init(porta_decoder_t *d);

// Feeds one byte. Returns true exactly when `out` has been filled with a
// complete, CRC-valid frame.
bool porta_decoder_push(porta_decoder_t *d, uint8_t byte, porta_frame_t *out);

// --- HELLO payload -------------------------------------------------------
// protocol_version (1 byte) + build_version (32 bytes, NUL-padded, NOT
// necessarily NUL-terminated -- a 32-character version fills the field).

#define PORTA_HELLO_VERSION_LEN 32u
#define PORTA_HELLO_PAYLOAD_LEN (1u + PORTA_HELLO_VERSION_LEN)

size_t porta_proto_encode_hello(uint8_t protocol_version, const char *build_version,
                                uint8_t *out, size_t out_cap);

// `build_version_out` must hold PORTA_HELLO_VERSION_LEN + 1 bytes; it is always
// NUL-terminated on success even when the wire field is not. False if the frame
// is not a well-formed HELLO.
bool porta_proto_parse_hello(const porta_frame_t *f, uint8_t *protocol_version_out,
                             char *build_version_out);

// --- EVENT payloads ------------------------------------------------------
// EVENT is a namespace, not a single message: the first payload byte is the
// subtype and the rest is subtype-specific. New telemetry is a new subtype, not
// a new frame type, so the type byte stops growing (RFC 0004 §6).
//
// Every event carries a timestamp in the envelope rather than in each subtype's
// body, so a new subtype inherits one without thinking about it. It is the
// moment the event *happened*, stamped by the host at emit -- not the moment it
// arrived. Those differ: the outbound queue can hold events through a burst,
// and a viewer that stamped on arrival would compress a whole slot's decodes
// onto whatever second they finished draining.
//
// The sidekick has no clock of its own -- no RTC, no SNTP -- so the host is the
// only possible source. Zero means "the host does not know the time either",
// which is a real state on a cold unit with no GPS or DS3231, and must render
// as blank rather than as 1970.
//
// Text fields run to the end of the frame rather than being padded to a fixed
// width -- the frame already carries a length, so padding would only cost wire
// bytes to say something already known.

typedef enum {
    PORTA_EVT_LOG    = 0x01,  // one line of the host's on-screen log
    PORTA_EVT_DECODE = 0x02,  // one decoded FT8/FT4 message
} porta_event_subtype_t;

#define PORTA_EVENT_TEXT_MAX 64  // matches RX_TEXT_MAX on the host

// `epoch_secs` is UTC seconds, or 0 when the host's clock is not set.
size_t porta_proto_encode_log(uint32_t epoch_secs, const char *text,
                              uint8_t *out, size_t out_cap);

// `text_out` must hold PORTA_EVENT_TEXT_MAX + 1 bytes. Always NUL-terminated
// on success. `epoch_secs_out` may be NULL. False if the frame is not a
// well-formed log event.
bool porta_proto_parse_log(const porta_frame_t *f, uint32_t *epoch_secs_out,
                           char *text_out);

typedef struct {
    uint32_t epoch_secs;   // UTC seconds, 0 when the host's clock is not set
    char     text[PORTA_EVENT_TEXT_MAX + 1];
    int8_t   snr;          // FT8 reports span roughly -24..+50, so int8 is ample
    uint16_t offset_hz;    // 200..3000 in practice
    int16_t  dt_centis;    // time offset in hundredths of a second, signed
    bool     is_cq;
    bool     is_to_me;
    bool     is_recent_qso;
} porta_decode_event_t;

size_t porta_proto_encode_decode(const porta_decode_event_t *d, uint8_t *out, size_t out_cap);
bool porta_proto_parse_decode(const porta_frame_t *f, porta_decode_event_t *out);

#ifdef __cplusplus
}
#endif
