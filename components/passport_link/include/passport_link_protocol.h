#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_LINK_PROTOCOL_VERSION 1
#define PASSPORT_LINK_HEADER_SIZE 36
#define PASSPORT_LINK_MAX_PAYLOAD 200

typedef enum {
    PASSPORT_LINK_TYPE_MESSAGE = 1,
    PASSPORT_LINK_TYPE_FILE_META = 2,
    PASSPORT_LINK_TYPE_FILE_CHUNK = 3,
    PASSPORT_LINK_TYPE_STREAM = 4,
} passport_link_type_t;

typedef struct {
    uint8_t type;
    uint64_t source_id;
    uint64_t target_id;
    uint32_t service;
    uint32_t sequence;
    uint16_t payload_len;
    const uint8_t *payload;
} passport_link_frame_t;

uint32_t passport_link_service_id(const char *name);

/** Encode one bounded Passport Link frame. The frame always includes source and target IDs. */
esp_err_t passport_link_frame_encode(uint8_t type, uint64_t source_id, uint64_t target_id,
                                     uint32_t service, uint32_t sequence,
                                     const void *payload, size_t payload_len,
                                     uint8_t *out, size_t out_capacity, size_t *out_len);

/** Decode and CRC-check one frame. payload points into the caller's input buffer. */
esp_err_t passport_link_frame_decode(const uint8_t *data, size_t len, passport_link_frame_t *out);
