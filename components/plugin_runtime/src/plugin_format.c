#include "plugin_format.h"

#include "plugin_theme.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    uint16_t first;
    uint16_t last;
} plugin_charset_range_t;

#include "plugin_charset_data.inc"

uint16_t plugin_format_read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint32_t plugin_format_read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

int16_t plugin_format_read_i16(const uint8_t *data)
{
    uint16_t bits = plugin_format_read_u16(data);
    int16_t value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

int32_t plugin_format_read_i32(const uint8_t *data)
{
    uint32_t bits = plugin_format_read_u32(data);
    int32_t value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool fixed_identifier_valid(const uint8_t *data, size_t size)
{
    size_t length = strnlen((const char *)data, size);

    if (length == 0U || length == size) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        unsigned char value = data[index];
        if (value < 0x20U || value > 0x7eU) {
            return false;
        }
        if (!(isalnum(value) || value == '_' || value == '-' || value == '.')) {
            return false;
        }
    }
    return true;
}

static bool bytes_are_zero(const uint8_t *data, size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        if (data[index] != 0U) return false;
    }
    return true;
}

static bool plugin_codepoint_supported(uint32_t codepoint)
{
    if (codepoint == '\n' || (codepoint >= 0x20U && codepoint <= 0x7eU)) return true;
    if (codepoint > UINT16_MAX) return false;

    size_t low = 0U;
    size_t high = PLUGIN_CHARSET_RANGE_COUNT;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        const plugin_charset_range_t *range = &PLUGIN_CHARSET_RANGES[middle];
        if (codepoint < range->first) high = middle;
        else if (codepoint > range->last) low = middle + 1U;
        else return true;
    }
    return false;
}

static bool utf8_text_valid(const uint8_t *data, size_t length, bool allow_newline)
{
    size_t index = 0U;

    while (index < length) {
        uint8_t lead = data[index];
        uint32_t codepoint;
        if (lead >= 0x20U && lead <= 0x7eU) {
            codepoint = lead;
            ++index;
        } else if (lead == '\n' && allow_newline) {
            codepoint = lead;
            ++index;
        } else if (lead >= 0xc2U && lead <= 0xdfU) {
            if (index + 1U >= length || data[index + 1U] < 0x80U ||
                data[index + 1U] > 0xbfU ||
                (lead == 0xc2U && data[index + 1U] < 0xa0U)) {
                return false;
            }
            codepoint = ((uint32_t)(lead & 0x1fU) << 6) |
                        (uint32_t)(data[index + 1U] & 0x3fU);
            index += 2U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            if (index + 2U >= length || data[index + 1U] < 0x80U ||
                data[index + 1U] > 0xbfU || data[index + 2U] < 0x80U ||
                data[index + 2U] > 0xbfU ||
                (lead == 0xe0U && data[index + 1U] < 0xa0U) ||
                (lead == 0xedU && data[index + 1U] > 0x9fU)) {
                return false;
            }
            codepoint = ((uint32_t)(lead & 0x0fU) << 12) |
                        ((uint32_t)(data[index + 1U] & 0x3fU) << 6) |
                        (uint32_t)(data[index + 2U] & 0x3fU);
            index += 3U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            if (index + 3U >= length || data[index + 1U] < 0x80U ||
                data[index + 1U] > 0xbfU || data[index + 2U] < 0x80U ||
                data[index + 2U] > 0xbfU || data[index + 3U] < 0x80U ||
                data[index + 3U] > 0xbfU ||
                (lead == 0xf0U && data[index + 1U] < 0x90U) ||
                (lead == 0xf4U && data[index + 1U] > 0x8fU)) {
                return false;
            }
            codepoint = ((uint32_t)(lead & 0x07U) << 18) |
                        ((uint32_t)(data[index + 1U] & 0x3fU) << 12) |
                        ((uint32_t)(data[index + 2U] & 0x3fU) << 6) |
                        (uint32_t)(data[index + 3U] & 0x3fU);
            index += 4U;
        } else {
            return false;
        }
        if (!plugin_codepoint_supported(codepoint)) return false;
    }
    return true;
}

static bool fixed_utf8_valid(const uint8_t *data, size_t size)
{
    size_t length = strnlen((const char *)data, size);
    return length != 0U && length != size && utf8_text_valid(data, length, false);
}

static bool string_table_valid(const uint8_t *data, size_t size)
{
    size_t offset = 0U;
    while (offset < size) {
        size_t remaining = size - offset;
        size_t length = strnlen((const char *)data + offset, remaining);
        if (length == remaining || !utf8_text_valid(data + offset, length, true)) return false;
        offset += length + 1U;
    }
    return true;
}

plugin_format_result_t plugin_format_parse_package_header(
    const uint8_t *data, size_t size, plugin_package_header_t *header)
{
    if (!data || !header) {
        return PLUGIN_FORMAT_INVALID_ARGUMENT;
    }
    if (size < PLUGIN_PACKAGE_HEADER_SIZE) {
        return PLUGIN_FORMAT_TRUNCATED;
    }
    if (memcmp(data, PLUGIN_PACKAGE_MAGIC, 4U) != 0) {
        return PLUGIN_FORMAT_BAD_MAGIC;
    }

    memset(header, 0, sizeof(*header));
    header->format_version = plugin_format_read_u16(data + 4U);
    header->header_size = plugin_format_read_u16(data + 6U);
    header->content_size = plugin_format_read_u32(data + 8U);
    memcpy(header->digest, data + 12U, sizeof(header->digest));
    memcpy(header->signature, data + 44U, sizeof(header->signature));

    if (header->format_version != PLUGIN_PACKAGE_VERSION) {
        return PLUGIN_FORMAT_UNSUPPORTED_VERSION;
    }
    if (header->header_size != PLUGIN_PACKAGE_HEADER_SIZE ||
        header->content_size < PLUGIN_MANIFEST_SIZE ||
        (uint64_t)header->header_size + header->content_size > size) {
        return PLUGIN_FORMAT_INVALID_LAYOUT;
    }
    return PLUGIN_FORMAT_OK;
}

plugin_format_result_t plugin_format_parse_manifest(
    const uint8_t *data, size_t size, plugin_manifest_t *manifest)
{
    if (!data || !manifest) {
        return PLUGIN_FORMAT_INVALID_ARGUMENT;
    }
    if (size < PLUGIN_MANIFEST_SIZE) {
        return PLUGIN_FORMAT_TRUNCATED;
    }
    if (memcmp(data, PLUGIN_MANIFEST_MAGIC, 4U) != 0) {
        return PLUGIN_FORMAT_BAD_MAGIC;
    }

    memset(manifest, 0, sizeof(*manifest));
    manifest->manifest_version = plugin_format_read_u16(data + 4U);
    manifest->bytecode_version = plugin_format_read_u16(data + 6U);
    manifest->host_api_version = plugin_format_read_u16(data + 8U);
    manifest->kind = data[10U];
    manifest->payload_schema = data[11U];
    manifest->plugin_version = plugin_format_read_u32(data + 12U);
    manifest->permissions = plugin_format_read_u32(data + 16U);
    manifest->state_slots = plugin_format_read_u16(data + 24U);
    manifest->code_size = plugin_format_read_u32(data + 28U);
    manifest->strings_size = plugin_format_read_u32(data + 32U);
    for (size_t index = 0; index < PLUGIN_EVENT_COUNT; ++index) {
        manifest->handlers[index] = plugin_format_read_u32(
            data + PLUGIN_MANIFEST_HANDLERS_OFFSET + index * sizeof(uint32_t));
    }
    memcpy(manifest->id, data + PLUGIN_MANIFEST_ID_OFFSET, PLUGIN_ID_SIZE);
    memcpy(manifest->name, data + PLUGIN_MANIFEST_NAME_OFFSET, PLUGIN_NAME_SIZE);
    memcpy(manifest->author, data + PLUGIN_MANIFEST_AUTHOR_OFFSET, PLUGIN_AUTHOR_SIZE);
    manifest->id[PLUGIN_ID_SIZE - 1U] = '\0';
    manifest->name[PLUGIN_NAME_SIZE - 1U] = '\0';
    manifest->author[PLUGIN_AUTHOR_SIZE - 1U] = '\0';

    if (manifest->manifest_version != PLUGIN_MANIFEST_VERSION ||
        manifest->bytecode_version != PLUGIN_BYTECODE_VERSION ||
        manifest->host_api_version != PLUGIN_HOST_API_VERSION) {
        return PLUGIN_FORMAT_UNSUPPORTED_VERSION;
    }
    if (manifest->plugin_version == 0U ||
        (manifest->permissions & ~PLUGIN_PERMISSION_MASK) != 0U ||
        manifest->state_slots > PLUGIN_STATE_SLOTS_MAX ||
        manifest->code_size == 0U || manifest->kind >= PLUGIN_KIND_COUNT ||
        !bytes_are_zero(data + 20U, 4U) || !bytes_are_zero(data + 26U, 2U)) {
        return PLUGIN_FORMAT_INVALID_LAYOUT;
    }
    if ((manifest->kind == PLUGIN_KIND_APP && manifest->payload_schema != 0U) ||
        (manifest->kind == PLUGIN_KIND_THEME &&
         (manifest->payload_schema != PLUGIN_THEME_VERSION ||
          manifest->permissions != 0U || manifest->state_slots != 0U))) {
        return PLUGIN_FORMAT_INVALID_LAYOUT;
    }
    if (!fixed_identifier_valid(data + PLUGIN_MANIFEST_ID_OFFSET, PLUGIN_ID_SIZE) ||
        !fixed_utf8_valid(data + PLUGIN_MANIFEST_NAME_OFFSET, PLUGIN_NAME_SIZE) ||
        !fixed_utf8_valid(data + PLUGIN_MANIFEST_AUTHOR_OFFSET, PLUGIN_AUTHOR_SIZE)) {
        return PLUGIN_FORMAT_INVALID_TEXT;
    }
    return PLUGIN_FORMAT_OK;
}

plugin_format_result_t plugin_format_validate_manifest_layout(
    const plugin_manifest_t *manifest, size_t content_size)
{
    if (!manifest) return PLUGIN_FORMAT_INVALID_ARGUMENT;
    if ((uint64_t)PLUGIN_MANIFEST_SIZE + manifest->code_size + manifest->strings_size !=
        content_size) {
        return PLUGIN_FORMAT_INVALID_LAYOUT;
    }
    for (size_t index = 0; index < PLUGIN_EVENT_COUNT; ++index) {
        uint32_t handler = manifest->handlers[index];
        if (handler != PLUGIN_HANDLER_NONE && handler >= manifest->code_size) {
            return PLUGIN_FORMAT_INVALID_LAYOUT;
        }
    }
    if (manifest->kind == PLUGIN_KIND_THEME) {
        if (manifest->code_size != PLUGIN_THEME_SIZE || manifest->strings_size != 0U) {
            return PLUGIN_FORMAT_INVALID_LAYOUT;
        }
        for (size_t index = 0; index < PLUGIN_EVENT_COUNT; ++index) {
            if (manifest->handlers[index] != PLUGIN_HANDLER_NONE) {
                return PLUGIN_FORMAT_INVALID_LAYOUT;
            }
        }
    }
    return PLUGIN_FORMAT_OK;
}

plugin_format_result_t plugin_format_validate_content(
    const uint8_t *content, size_t content_size, plugin_manifest_t *manifest)
{
    plugin_format_result_t result = plugin_format_parse_manifest(content, content_size, manifest);
    if (result != PLUGIN_FORMAT_OK) return result;

    result = plugin_format_validate_manifest_layout(manifest, content_size);
    if (result != PLUGIN_FORMAT_OK) return result;

    const uint8_t *payload = content + PLUGIN_MANIFEST_SIZE;
    if (manifest->kind == PLUGIN_KIND_THEME) {
        plugin_theme_descriptor_t theme;
        if (plugin_theme_parse(payload, manifest->code_size, &theme) != PLUGIN_THEME_OK) {
            return PLUGIN_FORMAT_INVALID_LAYOUT;
        }
    }
    const uint8_t *strings = payload + manifest->code_size;
    if (!string_table_valid(strings, manifest->strings_size)) {
        return PLUGIN_FORMAT_INVALID_TEXT;
    }
    return PLUGIN_FORMAT_OK;
}

const char *plugin_format_string_at(const uint8_t *strings, size_t strings_size,
                                    uint16_t offset)
{
    if (!strings || offset >= strings_size) {
        return NULL;
    }
    const uint8_t *start = strings + offset;
    if (!memchr(start, '\0', strings_size - offset)) {
        return NULL;
    }
    return (const char *)start;
}
