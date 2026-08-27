#include "passport_link_protocol.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char payload[] = "hello";
    uint8_t encoded[PASSPORT_LINK_HEADER_SIZE + PASSPORT_LINK_MAX_PAYLOAD];
    size_t encoded_len = 0;
    const uint64_t source = 0x001122334455ULL;
    const uint64_t target = 0x00AABBCCDDEEULL;
    const uint32_t service = passport_link_service_id("com.folotoy.test-app");

    assert(service != 0);
    assert(passport_link_frame_encode(PASSPORT_LINK_TYPE_MESSAGE, source, target,
                                      service, 7, payload, sizeof(payload) - 1,
                                      encoded, sizeof(encoded), &encoded_len) == ESP_OK);
    assert(encoded_len == PASSPORT_LINK_HEADER_SIZE + sizeof(payload) - 1);

    passport_link_frame_t frame;
    assert(passport_link_frame_decode(encoded, encoded_len, &frame) == ESP_OK);
    assert(frame.type == PASSPORT_LINK_TYPE_MESSAGE);
    assert(frame.source_id == source);
    assert(frame.target_id == target);
    assert(frame.service == service);
    assert(frame.sequence == 7);
    assert(frame.payload_len == sizeof(payload) - 1);
    assert(memcmp(frame.payload, payload, sizeof(payload) - 1) == 0);

    encoded[encoded_len - 1] ^= 0x01;
    assert(passport_link_frame_decode(encoded, encoded_len, &frame) == ESP_ERR_INVALID_CRC);
    assert(passport_link_frame_decode(encoded, PASSPORT_LINK_HEADER_SIZE - 1, &frame) == ESP_ERR_INVALID_ARG);

    puts("Passport Link protocol host tests: PASS");
    return 0;
}
