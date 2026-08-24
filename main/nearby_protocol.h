#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEARBY_PROTOCOL_VERSION 1U
#define NEARBY_FRAME_HEADER_SIZE 16U
#define NEARBY_FRAME_PAYLOAD_MAX 237U

#define NEARBY_FRAME_FLAG_FIRST (1U << 0)
#define NEARBY_FRAME_FLAG_LAST  (1U << 1)
#define NEARBY_FRAME_FLAG_ACCEPT (1U << 2)

#define NEARBY_VOICE_SAMPLE_RATE 16000U
#define NEARBY_VOICE_SAMPLES 320U
#define NEARBY_VOICE_BLOCK_SIZE 164U

typedef enum {
    NEARBY_FRAME_MESSAGE = 1,
    NEARBY_FRAME_BLOB_OFFER,
    NEARBY_FRAME_BLOB_DECISION,
    NEARBY_FRAME_BLOB_DATA,
    NEARBY_FRAME_BLOB_COMPLETE,
    NEARBY_FRAME_BLOB_CANCEL,
    NEARBY_FRAME_VOICE,
    NEARBY_FRAME_ACK,
    NEARBY_FRAME_ERROR,
} nearby_frame_type_t;

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint32_t id;
    uint32_t offset;
    uint32_t total;
    const uint8_t *payload;
    size_t payload_size;
} nearby_frame_t;

size_t nearby_frame_encode(uint8_t *output, size_t capacity,
                           uint8_t type, uint8_t flags, uint32_t id,
                           uint32_t offset, uint32_t total,
                           const uint8_t *payload, size_t payload_size);
bool nearby_frame_decode(const uint8_t *data, size_t size, nearby_frame_t *frame);

size_t nearby_adpcm_encode(const int16_t input[NEARBY_VOICE_SAMPLES],
                           uint8_t output[NEARBY_VOICE_BLOCK_SIZE]);
bool nearby_adpcm_decode(const uint8_t input[NEARBY_VOICE_BLOCK_SIZE],
                         int16_t output[NEARBY_VOICE_SAMPLES]);
