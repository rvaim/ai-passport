#include "plugin_format.h"
#include "plugin_theme.h"
#include "plugin_vm.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void write_u16(uint8_t *data, uint16_t value)
{
    data[0] = value & 0xffU;
    data[1] = value >> 8;
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = value & 0xffU;
    data[1] = (value >> 8) & 0xffU;
    data[2] = (value >> 16) & 0xffU;
    data[3] = (value >> 24) & 0xffU;
}

static void append_push(uint8_t *code, size_t *size, int32_t value)
{
    code[(*size)++] = 0x01;
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    write_u32(code + *size, bits);
    *size += 4U;
}

static void initialize_manifest(uint8_t *content, const char *id, const char *name,
                                uint8_t kind, uint8_t payload_schema,
                                uint32_t permissions, uint16_t state_slots,
                                uint32_t code_size, uint32_t strings_size,
                                const uint32_t handlers[PLUGIN_EVENT_COUNT])
{
    memcpy(content, PLUGIN_MANIFEST_MAGIC, 4U);
    write_u16(content + 4U, PLUGIN_MANIFEST_VERSION);
    write_u16(content + 6U, PLUGIN_BYTECODE_VERSION);
    write_u16(content + 8U, PLUGIN_HOST_API_VERSION);
    content[10U] = kind;
    content[11U] = payload_schema;
    write_u32(content + 12U, 1U);
    write_u32(content + 16U, permissions);
    write_u16(content + 24U, state_slots);
    write_u32(content + 28U, code_size);
    write_u32(content + 32U, strings_size);
    for (size_t index = 0; index < PLUGIN_EVENT_COUNT; ++index) {
        write_u32(content + PLUGIN_MANIFEST_HANDLERS_OFFSET + index * 4U,
                  handlers[index]);
    }
    memcpy(content + PLUGIN_MANIFEST_ID_OFFSET, id, strlen(id) + 1U);
    memcpy(content + PLUGIN_MANIFEST_NAME_OFFSET, name, strlen(name) + 1U);
    memcpy(content + PLUGIN_MANIFEST_AUTHOR_OFFSET, "FoloToy", sizeof("FoloToy"));
}

typedef struct {
    unsigned loads;
    unsigned saves;
    unsigned device_views;
} setting_host_t;

static bool setting_get(void *context, uint8_t setting_id, int32_t *value)
{
    setting_host_t *host = context;
    assert(setting_id == PLUGIN_SETTING_BRIGHTNESS);
    ++host->loads;
    *value = 70;
    return true;
}

static bool setting_set(void *context, uint8_t setting_id, int32_t value)
{
    setting_host_t *host = context;
    assert(setting_id == PLUGIN_SETTING_BRIGHTNESS);
    assert(value == 70);
    ++host->saves;
    return true;
}

static bool device_info(void *context)
{
    setting_host_t *host = context;
    ++host->device_views;
    return true;
}

static size_t build_content(uint8_t *content, size_t capacity)
{
    assert(capacity >= PLUGIN_MANIFEST_SIZE + 64U);
    memset(content, 0, capacity);
    uint8_t *code = content + PLUGIN_MANIFEST_SIZE;
    size_t code_size = 0;
    uint32_t handlers[PLUGIN_EVENT_COUNT];
    for (size_t index = 0; index < PLUGIN_EVENT_COUNT; ++index) {
        handlers[index] = PLUGIN_HANDLER_NONE;
    }

    handlers[PLUGIN_EVENT_START] = code_size;
    append_push(code, &code_size, INT32_MAX);
    append_push(code, &code_size, 1);
    code[code_size++] = 0x04;
    code[code_size++] = 0x03;
    code[code_size++] = 0;
    code[code_size++] = 0x00;

    handlers[PLUGIN_EVENT_UP] = code_size;
    append_push(code, &code_size, INT32_MIN);
    append_push(code, &code_size, -1);
    code[code_size++] = 0x07;
    code[code_size++] = 0x03;
    code[code_size++] = 0;
    code[code_size++] = 0x00;

    handlers[PLUGIN_EVENT_DOWN] = code_size;
    code[code_size++] = 0x10;
    write_u16(code + code_size, (uint16_t)-100);
    code_size += 2U;
    code[code_size++] = 0x00;

    handlers[PLUGIN_EVENT_OK] = code_size;
    code[code_size++] = 0x30;
    write_u16(code + code_size, 880);
    write_u16(code + code_size + 2U, 20);
    code_size += 4U;
    code[code_size++] = 0x00;

    handlers[PLUGIN_EVENT_TIMER0] = code_size;
    code[code_size++] = 0x10;
    write_u16(code + code_size, (uint16_t)-3);
    code_size += 2U;
    code[code_size++] = 0x00;

    handlers[PLUGIN_EVENT_TIMER1] = code_size;
    code[code_size++] = 0x35;
    code[code_size++] = PLUGIN_SETTING_BRIGHTNESS;
    code[code_size++] = 0;
    code[code_size++] = 0x36;
    code[code_size++] = PLUGIN_SETTING_BRIGHTNESS;
    code[code_size++] = 0;
    code[code_size++] = 0x00;

    handlers[PLUGIN_EVENT_TIMER2] = code_size;
    code[code_size++] = 0x26;
    code[code_size++] = 0x00;

    handlers[PLUGIN_EVENT_BACK] = code_size;
    code[code_size++] = 0x00;

    initialize_manifest(content, "test.runtime", "Runtime test", PLUGIN_KIND_APP, 0U,
                        PLUGIN_PERMISSION_SETTINGS, 1U, (uint32_t)code_size, 0U,
                        handlers);
    return PLUGIN_MANIFEST_SIZE + code_size;
}

static void test_theme_content(void)
{
    uint8_t content[PLUGIN_MANIFEST_SIZE + PLUGIN_THEME_SIZE] = {0};
    uint8_t *payload = content + PLUGIN_MANIFEST_SIZE;
    plugin_manifest_t manifest;
    plugin_theme_descriptor_t theme;
    plugin_vm_t vm;
    plugin_vm_host_t empty_host = {0};

    uint32_t handlers[PLUGIN_EVENT_COUNT];
    for (size_t index = 0; index < PLUGIN_EVENT_COUNT; ++index) {
        handlers[index] = PLUGIN_HANDLER_NONE;
    }
    initialize_manifest(content, "theme.runtime", "Runtime theme", PLUGIN_KIND_THEME,
                        PLUGIN_THEME_VERSION, 0U, 0U, PLUGIN_THEME_SIZE, 0U,
                        handlers);

    memcpy(payload, PLUGIN_THEME_MAGIC, 4U);
    write_u16(payload + 4U, PLUGIN_THEME_VERSION);
    write_u16(payload + 6U, PLUGIN_THEME_SIZE);
    for (size_t index = 0; index < PLUGIN_THEME_COLOR_COUNT; ++index) {
        write_u32(payload + 8U + index * 4U, 0x102030U + (uint32_t)index);
    }
    payload[56U] = 8U;
    payload[57U] = 2U;
    payload[58U] = 3U;
    payload[59U] = 2U;
    payload[60U] = 3U;
    payload[61U] = PLUGIN_THEME_DECORATION_NONE;

    assert(plugin_format_validate_content(content, sizeof(content), &manifest) ==
           PLUGIN_FORMAT_OK);
    assert(manifest.kind == PLUGIN_KIND_THEME);
    assert(plugin_theme_parse(payload, PLUGIN_THEME_SIZE, &theme) == PLUGIN_THEME_OK);
    assert(theme.panel_radius == 8U && theme.colors[PLUGIN_THEME_COLOR_TEXT] == 0x102032U);
    assert(plugin_vm_init(&vm, content, sizeof(content), &empty_host) ==
           PLUGIN_VM_INVALID_ARGUMENT);

    payload[56U] = 17U;
    assert(plugin_format_validate_content(content, sizeof(content), &manifest) ==
           PLUGIN_FORMAT_INVALID_LAYOUT);
}

int main(void)
{
    test_theme_content();
    uint8_t content[PLUGIN_MANIFEST_SIZE + 128U];
    size_t content_size = build_content(content, sizeof(content));
    plugin_manifest_t manifest;
    plugin_vm_t vm;
    setting_host_t setting_host = {0};
    plugin_vm_host_t host = {
        .context = &setting_host,
        .device_info = device_info,
        .setting_get = setting_get,
        .setting_set = setting_set,
    };

    assert(plugin_format_validate_content(content, content_size, &manifest) == PLUGIN_FORMAT_OK);
    assert(manifest.code_size == content_size - PLUGIN_MANIFEST_SIZE);
    assert(manifest.handlers[PLUGIN_EVENT_BACK] != PLUGIN_HANDLER_NONE);
    assert(plugin_format_validate_manifest_layout(&manifest, content_size) == PLUGIN_FORMAT_OK);
    assert(plugin_format_validate_manifest_layout(&manifest, content_size + 1U) ==
           PLUGIN_FORMAT_INVALID_LAYOUT);

    uint8_t *string_table = content + content_size;
    static const char supported_plugin_text[] = "插件";
    memcpy(string_table, supported_plugin_text, sizeof(supported_plugin_text));
    write_u32(content + 32U, sizeof(supported_plugin_text));
    assert(plugin_format_validate_content(
               content, content_size + sizeof(supported_plugin_text), &manifest) ==
           PLUGIN_FORMAT_OK);
    static const uint8_t unsupported_plugin_text[] = {
        0xf0U, 0xa0U, 0xaeU, 0xb7U, 0x00U,  // U+20BB7
    };
    memcpy(string_table, unsupported_plugin_text, sizeof(unsupported_plugin_text));
    write_u32(content + 32U, sizeof(unsupported_plugin_text));
    assert(plugin_format_validate_content(
               content, content_size + sizeof(unsupported_plugin_text), &manifest) ==
           PLUGIN_FORMAT_INVALID_TEXT);
    write_u32(content + 32U, 0U);

    static const char utf8_name[] = "计数器";
    memset(content + PLUGIN_MANIFEST_NAME_OFFSET, 0, PLUGIN_NAME_SIZE);
    memcpy(content + PLUGIN_MANIFEST_NAME_OFFSET, utf8_name, sizeof(utf8_name));
    assert(plugin_format_validate_content(content, content_size, &manifest) == PLUGIN_FORMAT_OK);
    content[PLUGIN_MANIFEST_NAME_OFFSET] = 0xe0U;
    content[PLUGIN_MANIFEST_NAME_OFFSET + 1U] = 0x80U;
    content[PLUGIN_MANIFEST_NAME_OFFSET + 2U] = 0x80U;
    assert(plugin_format_validate_content(content, content_size, &manifest) ==
           PLUGIN_FORMAT_INVALID_TEXT);
    memset(content + PLUGIN_MANIFEST_NAME_OFFSET, 0, PLUGIN_NAME_SIZE);
    memcpy(content + PLUGIN_MANIFEST_NAME_OFFSET, "Runtime test", sizeof("Runtime test"));

    write_u16(content + 4U, PLUGIN_MANIFEST_VERSION - 1U);
    assert(plugin_format_validate_content(content, content_size, &manifest) ==
           PLUGIN_FORMAT_UNSUPPORTED_VERSION);
    write_u16(content + 4U, PLUGIN_MANIFEST_VERSION);

    memcpy(content, "PLG1", 4U);
    assert(plugin_format_validate_content(content, content_size, &manifest) ==
           PLUGIN_FORMAT_BAD_MAGIC);
    memcpy(content, PLUGIN_MANIFEST_MAGIC, 4U);

    memcpy(content, "PLG4", 4U);
    assert(plugin_format_validate_content(content, content_size, &manifest) ==
           PLUGIN_FORMAT_BAD_MAGIC);
    memcpy(content, PLUGIN_MANIFEST_MAGIC, 4U);

    assert(plugin_vm_init(&vm, content, content_size, &host) == PLUGIN_VM_OK);

    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_START) == PLUGIN_VM_OK);
    assert(vm.state[0] == INT32_MIN);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_UP) == PLUGIN_VM_OK);
    assert(vm.state[0] == INT32_MIN);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_DOWN) == PLUGIN_VM_BAD_JUMP);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_PERMISSION_DENIED);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_TIMER0) == PLUGIN_VM_BUDGET_EXCEEDED);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_TIMER1) == PLUGIN_VM_OK);
    assert(setting_host.loads == 1U && setting_host.saves == 1U);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_TIMER2) == PLUGIN_VM_OK);
    assert(setting_host.device_views == 1U);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_TIMER3) == PLUGIN_VM_NO_HANDLER);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_BACK) == PLUGIN_VM_OK);

    write_u16(content + 8U, PLUGIN_HOST_API_VERSION - 1U);
    assert(plugin_format_validate_content(content, content_size, &manifest) ==
           PLUGIN_FORMAT_UNSUPPORTED_VERSION);
    write_u16(content + 8U, PLUGIN_HOST_API_VERSION);

    write_u32(content + 16U, 0U);
    assert(plugin_vm_init(&vm, content, content_size, &host) == PLUGIN_VM_OK);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_TIMER1) == PLUGIN_VM_PERMISSION_DENIED);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_TIMER2) == PLUGIN_VM_PERMISSION_DENIED);
    write_u32(content + 16U, PLUGIN_PERMISSION_SETTINGS);

    write_u32(content + 20U, 1U);
    assert(plugin_format_validate_content(content, content_size, &manifest) ==
           PLUGIN_FORMAT_INVALID_LAYOUT);
    write_u32(content + 20U, 0U);
    write_u32(content + 12U, 0U);
    assert(plugin_format_validate_content(content, content_size, &manifest) ==
           PLUGIN_FORMAT_INVALID_LAYOUT);
    write_u32(content + 12U, 1U);
    write_u32(content + 16U, 1U << 31);
    assert(plugin_format_validate_content(content, content_size, &manifest) ==
           PLUGIN_FORMAT_INVALID_LAYOUT);

    puts("plugin runtime host tests passed");
    return 0;
}
