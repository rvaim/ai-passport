#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PLUGIN_PACKAGE_MAGIC "FPP1"
#define PLUGIN_PACKAGE_VERSION 1U
#define PLUGIN_PACKAGE_HEADER_SIZE 108U
#define PLUGIN_PACKAGE_SIGNATURE_SIZE 64U
#define PLUGIN_PACKAGE_DIGEST_SIZE 32U

#define PLUGIN_MANIFEST_MAGIC "PLG5"
#define PLUGIN_MANIFEST_VERSION 5U
#define PLUGIN_MANIFEST_SIZE 192U
#define PLUGIN_MANIFEST_HANDLERS_OFFSET 36U
#define PLUGIN_MANIFEST_ID_OFFSET 80U
#define PLUGIN_MANIFEST_NAME_OFFSET 112U
#define PLUGIN_MANIFEST_AUTHOR_OFFSET 160U
#define PLUGIN_BYTECODE_VERSION 1U
#define PLUGIN_HOST_API_VERSION 5U

#define PLUGIN_ID_SIZE 32U
#define PLUGIN_NAME_SIZE 48U
#define PLUGIN_AUTHOR_SIZE 32U
#define PLUGIN_EVENT_COUNT 11U
#define PLUGIN_HANDLER_NONE UINT32_MAX
#define PLUGIN_STATE_SLOTS_MAX 16U
#define PLUGIN_PERMISSION_MASK \
    ((1U << 0) | (1U << 1) | (1U << 2) | (1U << 3) | (1U << 4))

typedef enum {
    PLUGIN_EVENT_START = 0,
    PLUGIN_EVENT_UP,
    PLUGIN_EVENT_DOWN,
    PLUGIN_EVENT_OK,
    PLUGIN_EVENT_TIMER0,
    PLUGIN_EVENT_TIMER1,
    PLUGIN_EVENT_TIMER2,
    PLUGIN_EVENT_TIMER3,
    PLUGIN_EVENT_BACK,
    PLUGIN_EVENT_ACTION,
    PLUGIN_EVENT_NEARBY,
} plugin_event_t;

typedef enum {
    PLUGIN_KIND_APP = 0,
    PLUGIN_KIND_THEME = 1,
    PLUGIN_KIND_COUNT,
} plugin_kind_t;

typedef enum {
    PLUGIN_PERMISSION_STORAGE = 1U << 0,
    PLUGIN_PERMISSION_AUDIO = 1U << 1,
    PLUGIN_PERMISSION_NEARBY = 1U << 2,
    PLUGIN_PERMISSION_SETTINGS = 1U << 3,
    PLUGIN_PERMISSION_MICROPHONE = 1U << 4,
} plugin_permission_t;

typedef enum {
    PLUGIN_SETTING_BRIGHTNESS = 0,
    PLUGIN_SETTING_VOLUME,
    PLUGIN_SETTING_KEY_SOUND,
    PLUGIN_SETTING_SCREEN_TIMEOUT,
    PLUGIN_SETTING_THEME,
    PLUGIN_SETTING_COUNT,
} plugin_setting_id_t;

typedef enum {
    PLUGIN_FORMAT_OK = 0,
    PLUGIN_FORMAT_INVALID_ARGUMENT,
    PLUGIN_FORMAT_TRUNCATED,
    PLUGIN_FORMAT_BAD_MAGIC,
    PLUGIN_FORMAT_UNSUPPORTED_VERSION,
    PLUGIN_FORMAT_INVALID_LAYOUT,
    PLUGIN_FORMAT_INVALID_TEXT,
} plugin_format_result_t;

typedef struct {
    uint16_t format_version;
    uint16_t header_size;
    uint32_t content_size;
    uint8_t digest[PLUGIN_PACKAGE_DIGEST_SIZE];
    uint8_t signature[PLUGIN_PACKAGE_SIGNATURE_SIZE];
} plugin_package_header_t;

typedef struct {
    uint16_t manifest_version;
    uint16_t bytecode_version;
    uint16_t host_api_version;
    uint8_t kind;
    uint8_t payload_schema;
    uint32_t plugin_version;
    uint32_t permissions;
    uint16_t state_slots;
    uint32_t code_size;
    uint32_t strings_size;
    uint32_t handlers[PLUGIN_EVENT_COUNT];
    char id[PLUGIN_ID_SIZE];
    char name[PLUGIN_NAME_SIZE];
    char author[PLUGIN_AUTHOR_SIZE];
} plugin_manifest_t;

plugin_format_result_t plugin_format_parse_package_header(
    const uint8_t *data, size_t size, plugin_package_header_t *header);
plugin_format_result_t plugin_format_parse_manifest(
    const uint8_t *data, size_t size, plugin_manifest_t *manifest);
plugin_format_result_t plugin_format_validate_manifest_layout(
    const plugin_manifest_t *manifest, size_t content_size);
plugin_format_result_t plugin_format_validate_content(
    const uint8_t *content, size_t content_size, plugin_manifest_t *manifest);
const char *plugin_format_string_at(const uint8_t *strings, size_t strings_size,
                                    uint16_t offset);
uint16_t plugin_format_read_u16(const uint8_t *data);
uint32_t plugin_format_read_u32(const uint8_t *data);
int16_t plugin_format_read_i16(const uint8_t *data);
int32_t plugin_format_read_i32(const uint8_t *data);
