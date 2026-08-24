#include "nearby_protocol.h"

#include <limits.h>
#include <string.h>

static const int16_t s_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767,
};

static const int8_t s_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

size_t nearby_frame_encode(uint8_t *output, size_t capacity,
                           uint8_t type, uint8_t flags, uint32_t id,
                           uint32_t offset, uint32_t total,
                           const uint8_t *payload, size_t payload_size)
{
    if (!output || type < NEARBY_FRAME_MESSAGE || type > NEARBY_FRAME_ERROR ||
        payload_size > NEARBY_FRAME_PAYLOAD_MAX ||
        (payload_size > 0U && !payload) ||
        capacity < NEARBY_FRAME_HEADER_SIZE + payload_size) {
        return 0U;
    }
    output[0] = NEARBY_PROTOCOL_VERSION;
    output[1] = type;
    output[2] = flags;
    output[3] = 0U;
    write_u32(output + 4U, id);
    write_u32(output + 8U, offset);
    write_u32(output + 12U, total);
    if (payload_size > 0U) {
        memcpy(output + NEARBY_FRAME_HEADER_SIZE, payload, payload_size);
    }
    return NEARBY_FRAME_HEADER_SIZE + payload_size;
}

bool nearby_frame_decode(const uint8_t *data, size_t size, nearby_frame_t *frame)
{
    if (!data || !frame || size < NEARBY_FRAME_HEADER_SIZE ||
        size > NEARBY_FRAME_HEADER_SIZE + NEARBY_FRAME_PAYLOAD_MAX ||
        data[0] != NEARBY_PROTOCOL_VERSION || data[3] != 0U ||
        data[1] < NEARBY_FRAME_MESSAGE || data[1] > NEARBY_FRAME_ERROR) {
        return false;
    }
    *frame = (nearby_frame_t) {
        .type = data[1],
        .flags = data[2],
        .id = read_u32(data + 4U),
        .offset = read_u32(data + 8U),
        .total = read_u32(data + 12U),
        .payload = data + NEARBY_FRAME_HEADER_SIZE,
        .payload_size = size - NEARBY_FRAME_HEADER_SIZE,
    };
    return true;
}

static int clamp_index(int index)
{
    if (index < 0) return 0;
    if (index > 88) return 88;
    return index;
}

static int16_t clamp_sample(int value)
{
    if (value < INT16_MIN) return INT16_MIN;
    if (value > INT16_MAX) return INT16_MAX;
    return (int16_t)value;
}

static uint8_t encode_nibble(int sample, int *predictor, int *index)
{
    int step = s_step_table[*index];
    int difference = sample - *predictor;
    uint8_t code = 0U;
    if (difference < 0) {
        code = 8U;
        difference = -difference;
    }
    int delta = step >> 3;
    if (difference >= step) {
        code |= 4U;
        difference -= step;
        delta += step;
    }
    if (difference >= (step >> 1)) {
        code |= 2U;
        difference -= step >> 1;
        delta += step >> 1;
    }
    if (difference >= (step >> 2)) {
        code |= 1U;
        delta += step >> 2;
    }
    *predictor = code & 8U ? *predictor - delta : *predictor + delta;
    *predictor = clamp_sample(*predictor);
    *index = clamp_index(*index + s_index_table[code]);
    return code;
}

static int16_t decode_nibble(uint8_t code, int *predictor, int *index)
{
    int step = s_step_table[*index];
    int delta = step >> 3;
    if (code & 4U) delta += step;
    if (code & 2U) delta += step >> 1;
    if (code & 1U) delta += step >> 2;
    *predictor = code & 8U ? *predictor - delta : *predictor + delta;
    *predictor = clamp_sample(*predictor);
    *index = clamp_index(*index + s_index_table[code & 0x0fU]);
    return (int16_t)*predictor;
}

size_t nearby_adpcm_encode(const int16_t input[NEARBY_VOICE_SAMPLES],
                           uint8_t output[NEARBY_VOICE_BLOCK_SIZE])
{
    if (!input || !output) return 0U;
    int predictor = input[0];
    int index = 0;
    output[0] = (uint8_t)predictor;
    output[1] = (uint8_t)((uint16_t)predictor >> 8);
    output[2] = (uint8_t)index;
    output[3] = 0U;
    memset(output + 4U, 0, NEARBY_VOICE_BLOCK_SIZE - 4U);
    for (size_t sample = 1U; sample < NEARBY_VOICE_SAMPLES; ++sample) {
        uint8_t code = encode_nibble(input[sample], &predictor, &index);
        size_t nibble = sample - 1U;
        if ((nibble & 1U) == 0U) output[4U + nibble / 2U] = code;
        else output[4U + nibble / 2U] |= (uint8_t)(code << 4);
    }
    return NEARBY_VOICE_BLOCK_SIZE;
}

bool nearby_adpcm_decode(const uint8_t input[NEARBY_VOICE_BLOCK_SIZE],
                         int16_t output[NEARBY_VOICE_SAMPLES])
{
    if (!input || !output || input[3] != 0U || input[2] > 88U) return false;
    int predictor = (int16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8));
    int index = input[2];
    output[0] = (int16_t)predictor;
    for (size_t sample = 1U; sample < NEARBY_VOICE_SAMPLES; ++sample) {
        size_t nibble = sample - 1U;
        uint8_t packed = input[4U + nibble / 2U];
        uint8_t code = (nibble & 1U) == 0U ? packed & 0x0fU : packed >> 4;
        output[sample] = decode_nibble(code, &predictor, &index);
    }
    return true;
}
