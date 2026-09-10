// Telemetry decode safety regression.
//
// ftx_message_decode() renders telemetry into field1 as two hex characters per
// payload byte plus a NUL. That is longer than any callsign or free-text field,
// and a hand-sized field1_buf[16] overflowed it by 3 bytes until the buffer was
// derived from FTX_TELEMETRY_HEX_LENGTH instead.

#include <cstdio>
#include <cstring>

extern "C" {
#include "../../components/ft8_lib/vendor/ft8/message.h"
}

int main()
{
    const uint8_t telemetry[FTX_TELEMETRY_LENGTH_BYTES] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01
    };

    ftx_message_t msg;
    ftx_message_init(&msg);
    if (ftx_message_encode_telemetry(&msg, telemetry) != FTX_MESSAGE_RC_OK)
        return 1;

    // Mark the payload as FT8 telemetry: i3=0, n3=5 (binary 101).
    msg.payload[8] = (uint8_t)((msg.payload[8] & 0xFEu) | 0x01u);
    msg.payload[9] = 0x40u;

    if (ftx_message_get_type(&msg) != FTX_MESSAGE_TYPE_TELEMETRY)
        return 2;

    char decoded[FTX_MAX_MESSAGE_LENGTH] = {0};
    ftx_message_offsets_t offsets = {};
    if (ftx_message_decode(&msg, nullptr, decoded, &offsets) != FTX_MESSAGE_RC_OK)
        return 3;

    const char* expected = "0123456789ABCDEF01";
    if (std::strcmp(decoded, expected) != 0)
    {
        std::fprintf(stderr, "telemetry decode: got='%s' expected='%s'\n",
                     decoded, expected);
        return 4;
    }

    if (std::strlen(decoded) != FTX_TELEMETRY_HEX_LENGTH)
    {
        std::fprintf(stderr, "telemetry decode: length %zu, expected %d\n",
                     std::strlen(decoded), (int)FTX_TELEMETRY_HEX_LENGTH);
        return 5;
    }

    std::printf("PASS: telemetry decode = %s (%d chars)\n",
                decoded, (int)FTX_TELEMETRY_HEX_LENGTH);
    return 0;
}
