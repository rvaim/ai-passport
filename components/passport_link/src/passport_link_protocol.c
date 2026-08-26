#include "passport_link_protocol.h"

#include "passport_crc32.h"
#include <string.h>

static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void put_le64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t get_le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t get_le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t get_le64(const uint8_t *p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | p[i]; return v; }

uint32_t passport_link_service_id(const char *name)
{
    uint32_t hash = 2166136261U;
    if (!name) return 0;
    while (*name) {
        hash ^= (uint8_t)*name++;
        hash *= 16777619U;
    }
    return hash;
}

esp_err_t passport_link_frame_encode(uint8_t type, uint64_t source_id, uint64_t target_id,
                                     uint32_t service, uint32_t sequence,
                                     const void *payload, size_t payload_len,
                                     uint8_t *out, size_t out_capacity, size_t *out_len)
{
    if (!out || !out_len || (payload_len && !payload) || payload_len > PASSPORT_LINK_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t total = PASSPORT_LINK_HEADER_SIZE + payload_len;
    if (out_capacity < total) return ESP_ERR_INVALID_SIZE;
    out[0] = 'P'; out[1] = 'L'; out[2] = PASSPORT_LINK_PROTOCOL_VERSION; out[3] = type;
    put_le64(out + 4, source_id);
    put_le64(out + 12, target_id);
    put_le32(out + 20, service);
    put_le32(out + 24, sequence);
    put_le16(out + 28, (uint16_t)payload_len);
    put_le16(out + 30, 0);
    put_le32(out + 32, passport_crc32(payload, payload_len));
    if (payload_len) memcpy(out + PASSPORT_LINK_HEADER_SIZE, payload, payload_len);
    *out_len = total;
    return ESP_OK;
}

esp_err_t passport_link_frame_decode(const uint8_t *data, size_t len, passport_link_frame_t *out)
{
    if (!data || !out || len < PASSPORT_LINK_HEADER_SIZE) return ESP_ERR_INVALID_ARG;
    if (data[0] != 'P' || data[1] != 'L' || data[2] != PASSPORT_LINK_PROTOCOL_VERSION) return ESP_ERR_INVALID_VERSION;
    uint16_t payload_len = get_le16(data + 28);
    if (payload_len > PASSPORT_LINK_MAX_PAYLOAD || len != (size_t)PASSPORT_LINK_HEADER_SIZE + (size_t)payload_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint8_t *payload = data + PASSPORT_LINK_HEADER_SIZE;
    if (passport_crc32(payload, payload_len) != get_le32(data + 32)) return ESP_ERR_INVALID_CRC;
    out->type = data[3];
    out->source_id = get_le64(data + 4);
    out->target_id = get_le64(data + 12);
    out->service = get_le32(data + 20);
    out->sequence = get_le32(data + 24);
    out->payload_len = payload_len;
    out->payload = payload;
    return ESP_OK;
}
