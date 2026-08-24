#include "nearby_protocol.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_frame_codec(void)
{
    static const uint8_t payload[] = {1U, 2U, 3U, 4U, 5U};
    uint8_t encoded[NEARBY_FRAME_HEADER_SIZE + sizeof(payload)];
    nearby_frame_t frame;

    size_t size = nearby_frame_encode(
        encoded, sizeof(encoded), NEARBY_FRAME_MESSAGE,
        NEARBY_FRAME_FLAG_FIRST | NEARBY_FRAME_FLAG_LAST,
        0x12345678U, 9U, 14U, payload, sizeof(payload));
    assert(size == sizeof(encoded));
    assert(nearby_frame_decode(encoded, size, &frame));
    assert(frame.type == NEARBY_FRAME_MESSAGE);
    assert(frame.flags == (NEARBY_FRAME_FLAG_FIRST | NEARBY_FRAME_FLAG_LAST));
    assert(frame.id == 0x12345678U && frame.offset == 9U && frame.total == 14U);
    assert(frame.payload_size == sizeof(payload));
    assert(memcmp(frame.payload, payload, sizeof(payload)) == 0);

    encoded[0] = 2U;
    assert(!nearby_frame_decode(encoded, size, &frame));
    encoded[0] = NEARBY_PROTOCOL_VERSION;
    encoded[3] = 1U;
    assert(!nearby_frame_decode(encoded, size, &frame));
    assert(nearby_frame_encode(encoded, sizeof(encoded), 0U, 0U, 0U, 0U,
                               0U, NULL, 0U) == 0U);
}

static void test_adpcm_codec(void)
{
    int16_t source[NEARBY_VOICE_SAMPLES];
    int16_t decoded[NEARBY_VOICE_SAMPLES];
    uint8_t encoded[NEARBY_VOICE_BLOCK_SIZE];

    memset(source, 0, sizeof(source));
    assert(nearby_adpcm_encode(source, encoded) == sizeof(encoded));
    assert(nearby_adpcm_decode(encoded, decoded));
    assert(memcmp(source, decoded, sizeof(source)) == 0);

    for (size_t index = 0; index < NEARBY_VOICE_SAMPLES; ++index) {
        source[index] = (int16_t)((int32_t)(index % 80U) * 500 - 20000);
    }
    assert(nearby_adpcm_encode(source, encoded) == sizeof(encoded));
    assert(nearby_adpcm_decode(encoded, decoded));
    int64_t absolute_error = 0;
    for (size_t index = 0; index < NEARBY_VOICE_SAMPLES; ++index) {
        int32_t difference = (int32_t)source[index] - decoded[index];
        absolute_error += difference < 0 ? -difference : difference;
    }
    assert(absolute_error / NEARBY_VOICE_SAMPLES < 1800);
    encoded[2] = 89U;
    assert(!nearby_adpcm_decode(encoded, decoded));
}

int main(void)
{
    test_frame_codec();
    test_adpcm_codec();
    puts("nearby protocol host tests passed");
    return 0;
}
