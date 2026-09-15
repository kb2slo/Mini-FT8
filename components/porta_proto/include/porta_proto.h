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
    // FILE_LIST / FILE_READ (sidekick -> host) request one page (skip/take)
    // of day-file names or parsed QSO rows; the reply is zero or more
    // FILE_DATA frames -- one row each -- followed by ACK/NAK carrying the
    // request's own type as verb. See the FILE_* payloads section.
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
    PORTA_EVT_LOG         = 0x01,  // one line of the host's on-screen log
    PORTA_EVT_DECODE      = 0x02,  // one decoded FT8/FT4 message
    PORTA_EVT_QUEUE_ENTRY = 0x03,  // one autoseq queue row: added, changed, or removed
    PORTA_EVT_SLOT_STATE  = 0x04,  // small scalars: slot parity, beacon mode, resolved TX offset
    PORTA_EVT_TX_HUD      = 0x05,  // TX HUD banner mirror: message, power/SWR, battery
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
    uint32_t decode_id;    // monotonic, assigned by the host at emit -- names this
                            // decode for a later QUEUE_REPLY action instead of making
                            // the browser re-parse rendered text back into a message
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

// --- QUEUE_ENTRY / SLOT_STATE payloads ------------------------------------
// Both are EVENT subtypes and share the subtype+epoch envelope, same as LOG
// and DECODE above.
//
// QUEUE_ENTRY reports one autoseq context. `state` is the wire form of
// AutoseqState (main/autoseq.h): 0=CALLING, 1=REPLYING, 2=REPORT,
// 3=ROGER_REPORT, 4=ROGERS, 5=SIGNOFF, 6=IDLE -- and IDLE *is* the removal
// signal, since that is already what the state means on the host (evicted by
// sort_and_clean). No separate "removed" flag is needed.
//
// `entry_id` is a stable identity assigned when the context is created, not
// a queue position: the queue re-sorts by priority (autoseq.cpp
// compare_ctx), so a browser snapshot's row index can point at a different
// context by the time a cancel arrives. QUEUE_CANCEL below addresses by this
// id for the same reason.
//
// AUTOSEQ_MAX_QUEUE is 30 (active + inactive), not a number that needs
// paging on its own -- the whole queue fits comfortably in individual
// per-change frames.

#define PORTA_CALLSIGN_MAX 16u

typedef struct {
    uint32_t epoch_secs;
    uint16_t entry_id;
    uint8_t  state;         // AutoseqState wire value
    uint8_t  retry_count;
    uint8_t  retry_limit;
    char     dxcall[PORTA_CALLSIGN_MAX + 1];
} porta_queue_entry_event_t;

size_t porta_proto_encode_queue_entry(const porta_queue_entry_event_t *q,
                                      uint8_t *out, size_t out_cap);
bool porta_proto_parse_queue_entry(const porta_frame_t *f, porta_queue_entry_event_t *out);

// SLOT_STATE is global scalars, not any one queue entry's: `slot_parity` is
// the *next* slot boundary (0=even, 1=odd), and `resolved_offset_hz` is what
// the next CQ/beacon transmission would use -- an established QSO's own
// offset already lives on its QsoContext and travels in QUEUE_ENTRY instead.
typedef struct {
    uint32_t epoch_secs;
    uint8_t  slot_parity;
    uint8_t  beacon_mode;        // BeaconMode wire value: 0=OFF, 1=EVEN, 2=ODD
    uint16_t resolved_offset_hz;
} porta_slot_state_event_t;

size_t porta_proto_encode_slot_state(const porta_slot_state_event_t *s,
                                     uint8_t *out, size_t out_cap);
bool porta_proto_parse_slot_state(const porta_frame_t *f, porta_slot_state_event_t *out);

// TX_HUD mirrors components/ui/include/tx_hud_banner.h's TxHudBannerInput --
// same raw fields the on-device banner already computes each ~500 ms while
// visible, not a re-derivation. power_w/swr travel as fixed-point (x10/x100)
// since the wire has no float; -1 means "not read yet" on every numeric
// field, the same sentinel TxHudBannerInput itself uses. `active` is the
// TX_HUD_LEN-carried banner-visible flag (tx_hud_visible(): TX running, or
// the post-abort linger window) -- the browser hides its panel the instant
// an event arrives with this false, rather than guessing from a timeout.
#define PORTA_TX_HUD_TEXT_MAX PORTA_EVENT_TEXT_MAX
#define PORTA_TX_HUD_REASON_MAX 24u

typedef struct {
    uint32_t epoch_secs;
    bool     active;
    bool     aborted;
    bool     writes_blocked;
    int16_t  power_dw;      // deciwatts (power_w * 10), -1 = unknown
    int16_t  swr_c;         // SWR * 100, -1 = unknown
    int8_t   battery_pct;   // -1 = unknown
    char     reason[PORTA_TX_HUD_REASON_MAX + 1];  // why `aborted`, e.g. "low battery";
                                                    // empty when not aborted -- sourced from
                                                    // the actual abort site, not guessed
                                                    // client-side, so a future second abort
                                                    // reason shows up as itself
    char     text[PORTA_TX_HUD_TEXT_MAX + 1];
} porta_tx_hud_event_t;

size_t porta_proto_encode_tx_hud(const porta_tx_hud_event_t *h, uint8_t *out, size_t out_cap);
bool porta_proto_parse_tx_hud(const porta_frame_t *f, porta_tx_hud_event_t *out);

// --- ACTION payloads -----------------------------------------------------
// ACTION is the other namespace: payload[0] is the verb, the rest is
// verb-specific. Actions travel sidekick -> host and are answered with ACK or
// NAK carrying the same verb, so a reply can be matched to its request without
// a sequence number -- adequate while one action is outstanding at a time,
// which is all the polled design permits.

typedef enum {
    PORTA_ACT_SET_CLOCK = 0x01,  // epoch seconds + milliseconds
    PORTA_ACT_TX_FREE   = 0x02,  // free-text one-shot for the next matching slot
    PORTA_ACT_TX_CANCEL = 0x03,  // abort in-flight / clear armed TX
    PORTA_ACT_CONNECT   = 0x04,  // start UAC + CAT sync (STATUS key 2)
    PORTA_ACT_TUNE      = 0x05,  // payload: u8 on (STATUS key 4)
    PORTA_ACT_BEACON       = 0x06,  // payload: u8 mode (STATUS key 1's 3-way cycle)
    PORTA_ACT_QUEUE_CANCEL = 0x07,  // payload: u16 entry_id (not a list index)
    PORTA_ACT_QUEUE_REPLY  = 0x08,  // payload: u32 decode_id
} porta_action_verb_t;

// The browser is the clock source in a headless build: it is the only device
// present that reliably knows the time, and unlike NTP it knows it without
// internet -- which is the case that matters, since a cold radio on a summit
// will not decode until UTC is right.
size_t porta_proto_encode_set_clock(uint32_t epoch_secs, uint16_t millis,
                                    uint8_t *out, size_t out_cap);
bool porta_proto_parse_set_clock(const porta_frame_t *f, uint32_t *epoch_secs_out,
                                 uint16_t *millis_out);

// Queue a free-text transmission the same way MENU "Send FreeText" does: one
// shot, inherits slot parity from the autoseq head (or the next slot). Text
// runs to the end of the frame; empty is refused at encode.
size_t porta_proto_encode_tx_free(const char *text, uint8_t *out, size_t out_cap);
bool porta_proto_parse_tx_free(const porta_frame_t *f, char *text_out);

// Cancel: verb only.
size_t porta_proto_encode_tx_cancel(uint8_t *out, size_t out_cap);
bool porta_proto_parse_tx_cancel(const porta_frame_t *f);

// Connect: verb only — same as STATUS → 2 (start RX audio + CAT sync).
size_t porta_proto_encode_connect(uint8_t *out, size_t out_cap);
bool porta_proto_parse_connect(const porta_frame_t *f);

// Tune: verb + u8 on (1 = TX tone, 0 = RX).
size_t porta_proto_encode_tune(bool on, uint8_t *out, size_t out_cap);
bool porta_proto_parse_tune(const porta_frame_t *f, bool *on_out);

// Beacon: verb + u8 mode. `mode` is BeaconMode's own wire value
// (0=OFF/1=EVEN/2=ODD, main.cpp's STATUS-key-1 cycle), so the browser sends
// back exactly the value it displayed, no translation either side.
size_t porta_proto_encode_beacon(uint8_t mode, uint8_t *out, size_t out_cap);
bool porta_proto_parse_beacon(const porta_frame_t *f, uint8_t *mode_out);

// Queue cancel: verb + u16 entry_id. See porta_queue_entry_event_t above for
// why this is an id and not a queue position.
size_t porta_proto_encode_queue_cancel(uint16_t entry_id, uint8_t *out, size_t out_cap);
bool porta_proto_parse_queue_cancel(const porta_frame_t *f, uint16_t *entry_id_out);

// Queue reply: verb + u32 decode_id, naming a decode the browser was shown
// rather than re-sending its text.
size_t porta_proto_encode_queue_reply(uint32_t decode_id, uint8_t *out, size_t out_cap);
bool porta_proto_parse_queue_reply(const porta_frame_t *f, uint32_t *decode_id_out);

size_t porta_proto_encode_ack(uint8_t verb, uint8_t *out, size_t out_cap);

// `reason` is short free text for a human; it reaches the operator's log, so it
// should say what was wrong rather than name a code.
size_t porta_proto_encode_nak(uint8_t verb, const char *reason,
                              uint8_t *out, size_t out_cap);

bool porta_proto_parse_ack(const porta_frame_t *f, uint8_t *verb_out);

// `reason_out` must hold PORTA_EVENT_TEXT_MAX + 1 bytes.
bool porta_proto_parse_nak(const porta_frame_t *f, uint8_t *verb_out, char *reason_out);

// --- CONFIG payloads -----------------------------------------------------
// CONFIG_* share one shape: key_len (1) + key[key_len] + value_bytes…
// CONFIG_GET with key_len == 0 means "all keys" (Station.txt surface).
// CONFIG_SET / CONFIG_VALUE require a non-empty key. Values are the same
// text Station.txt would store. ACK/NAK for SET (and end-of-GET-all) reuse
// PORTA_MSG_ACK/NAK with verb = the CONFIG message type (0x06 / 0x07).

#define PORTA_CONFIG_KEY_MAX 32u
#define PORTA_CONFIG_VALUE_MAX 200u

size_t porta_proto_encode_config_get(const char *key, uint8_t *out, size_t out_cap);
size_t porta_proto_encode_config_set(const char *key, const char *value,
                                     uint8_t *out, size_t out_cap);
size_t porta_proto_encode_config_value(const char *key, const char *value,
                                       uint8_t *out, size_t out_cap);

// `key_out` / `value_out` must hold KEY_MAX+1 / VALUE_MAX+1. Empty key on
// GET means get-all. SET/VALUE parsers require a non-empty key.
bool porta_proto_parse_config_get(const porta_frame_t *f, char *key_out);
bool porta_proto_parse_config_set(const porta_frame_t *f, char *key_out, char *value_out);
bool porta_proto_parse_config_value(const porta_frame_t *f, char *key_out, char *value_out);

// --- FILE_LIST / FILE_READ / FILE_DATA payloads ---------------------------
// FILE_LIST and FILE_READ (sidekick -> host) request one page -- skip/take,
// the same shape main/storage/qso_browse.h's QsoBrowsePager already uses --
// reusing that pager instead of inventing wire-level pagination a second
// time. The reply is zero or more FILE_DATA frames, one row each (so a short
// buffer can never leave a row half-written), followed by ACK/NAK carrying
// the request's own message type as verb -- the same "burst then ACK" shape
// CONFIG_GET-all already established. Fewer rows than `take` arriving before
// the ACK means end of list/file; the caller pages further with a higher
// `skip`, exactly like the pager's own `has_next`.
//
// FILE_READ ships *parsed* QSO rows (QsoLogEntry), never raw ADIF bytes --
// ADIF is verbose text and the parser already exists and is host-tested
// (qso_browse_pager_feed); re-parsing it in JavaScript would cost wire
// budget and correctness for nothing.
//
// mode/station_callsign (added for the browser's "Download ADI" button)
// exist for the same reason as every other field here: the browser
// reconstructs a real per-record ADIF line client-side from these rows, and
// both are required ADIF fields the record can't omit -- unlike grid/freq/
// comment, which are genuinely optional per QSO.

typedef enum {
    PORTA_FILE_LIST_QSO_DAILY = 0x01,  // matches FileListKind::QsoDaily
} porta_file_list_kind_t;

typedef enum {
    PORTA_FILE_DATA_NAME  = 0x01,  // one FILE_LIST row: a day-file name
    PORTA_FILE_DATA_ENTRY = 0x02,  // one FILE_READ row: one parsed QSO
} porta_file_data_kind_t;

#define PORTA_FILENAME_MAX 32u
#define PORTA_BAND_MAX 8u
#define PORTA_GRID_MAX 8u
#define PORTA_FREQ_MAX 12u
#define PORTA_COMMENT_MAX 48u
#define PORTA_MODE_MAX 7u  // "FT8"/"FT4" today; room for a longer protocol name

size_t porta_proto_encode_file_list_req(uint8_t kind, uint16_t skip, uint8_t take,
                                        uint8_t *out, size_t out_cap);
bool porta_proto_parse_file_list_req(const porta_frame_t *f, uint8_t *kind_out,
                                     uint16_t *skip_out, uint8_t *take_out);

// `filename_out` must hold PORTA_FILENAME_MAX + 1 bytes.
size_t porta_proto_encode_file_read_req(const char *filename, uint16_t skip, uint8_t take,
                                        uint8_t *out, size_t out_cap);
bool porta_proto_parse_file_read_req(const porta_frame_t *f, char *filename_out,
                                     uint16_t *skip_out, uint8_t *take_out);

// `name_out` must hold PORTA_FILENAME_MAX + 1 bytes.
size_t porta_proto_encode_file_name_row(const char *name, uint8_t *out, size_t out_cap);
bool porta_proto_parse_file_name_row(const porta_frame_t *f, char *name_out);

// time_on is "HH:MM" (qso_browse.cpp's own width -- "??:??" when unknown).
// rst_sent/rst_rcvd use -99 for "no report", the same sentinel
// QsoContext::snr_tx/snr_rx already use, rather than a separate has-flag.
typedef struct {
    char    time_on[6];
    char    band[PORTA_BAND_MAX + 1];
    char    call[PORTA_CALLSIGN_MAX + 1];
    int8_t  rst_sent;
    int8_t  rst_rcvd;
    char    grid[PORTA_GRID_MAX + 1];        // their grid (<gridsquare>), empty if absent
    char    freq[PORTA_FREQ_MAX + 1];        // raw ADIF MHz string, empty if absent
    char    my_grid[PORTA_GRID_MAX + 1];     // own grid at the time (<my_gridsquare>), empty if absent
    char    comment[PORTA_COMMENT_MAX + 1];  // <comment>, empty if absent
    char    mode[PORTA_MODE_MAX + 1];              // <mode>, e.g. "FT8"
    char    station_callsign[PORTA_CALLSIGN_MAX + 1]; // <station_callsign>, own call at log time
} porta_qso_entry_row_t;

size_t porta_proto_encode_file_entry_row(const porta_qso_entry_row_t *e,
                                         uint8_t *out, size_t out_cap);
bool porta_proto_parse_file_entry_row(const porta_frame_t *f, porta_qso_entry_row_t *out);

#ifdef __cplusplus
}
#endif
