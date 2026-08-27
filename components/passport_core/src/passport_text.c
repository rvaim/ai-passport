#include "passport_text.h"

#include <stdint.h>

bool passport_text_utf8_is_valid(const char *text, size_t length)
{
    if (!text && length != 0U) return false;
    const uint8_t *bytes = (const uint8_t *)text;
    size_t i = 0;
    while (i < length) {
        uint8_t first = bytes[i++];
        if (first <= 0x7FU) continue;
        if (first >= 0xC2U && first <= 0xDFU) {
            if (i >= length || (bytes[i++] & 0xC0U) != 0x80U) return false;
            continue;
        }
        if (first >= 0xE0U && first <= 0xEFU) {
            if (i + 1U >= length) return false;
            uint8_t second = bytes[i++];
            uint8_t third = bytes[i++];
            if ((second & 0xC0U) != 0x80U || (third & 0xC0U) != 0x80U ||
                (first == 0xE0U && second < 0xA0U) ||
                (first == 0xEDU && second >= 0xA0U)) {
                return false;
            }
            continue;
        }
        if (first >= 0xF0U && first <= 0xF4U) {
            if (i + 2U >= length) return false;
            uint8_t second = bytes[i++];
            uint8_t third = bytes[i++];
            uint8_t fourth = bytes[i++];
            if ((second & 0xC0U) != 0x80U || (third & 0xC0U) != 0x80U ||
                (fourth & 0xC0U) != 0x80U ||
                (first == 0xF0U && second < 0x90U) ||
                (first == 0xF4U && second >= 0x90U)) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

bool passport_text_json_contains_nul_escape(const char *json, size_t length)
{
    if (!json) return false;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = 0; i < length; ++i) {
        char current = json[i];
        if (!in_string) {
            if (current == '"') in_string = true;
            continue;
        }
        if (escaped) {
            if (current == 'u' && i + 4U < length &&
                json[i + 1U] == '0' && json[i + 2U] == '0' &&
                json[i + 3U] == '0' && json[i + 4U] == '0') {
                return true;
            }
            escaped = false;
        } else if (current == '\\') {
            escaped = true;
        } else if (current == '"') {
            in_string = false;
        }
    }
    return false;
}
