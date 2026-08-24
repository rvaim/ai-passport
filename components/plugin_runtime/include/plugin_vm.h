#pragma once

#include "plugin_format.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PLUGIN_VM_STACK_SIZE 16U
#define PLUGIN_VM_INSTRUCTION_BUDGET 512U

typedef enum {
    PLUGIN_VM_OK = 0,
    PLUGIN_VM_INVALID_ARGUMENT,
    PLUGIN_VM_NO_HANDLER,
    PLUGIN_VM_BAD_OPCODE,
    PLUGIN_VM_TRUNCATED,
    PLUGIN_VM_STACK_OVERFLOW,
    PLUGIN_VM_STACK_UNDERFLOW,
    PLUGIN_VM_BAD_STATE_SLOT,
    PLUGIN_VM_BAD_STRING,
    PLUGIN_VM_BAD_JUMP,
    PLUGIN_VM_DIVIDE_BY_ZERO,
    PLUGIN_VM_BUDGET_EXCEEDED,
    PLUGIN_VM_PERMISSION_DENIED,
    PLUGIN_VM_HOST_ERROR,
} plugin_vm_result_t;

typedef enum {
    PLUGIN_VM_ALIGN_LEFT = 0,
    PLUGIN_VM_ALIGN_CENTER = 1,
    PLUGIN_VM_ALIGN_RIGHT = 2,
} plugin_vm_align_t;

typedef enum {
    PLUGIN_UI_VALUE_NONE = 0,
    PLUGIN_UI_VALUE_TEXT,
    PLUGIN_UI_VALUE_INTEGER,
    PLUGIN_UI_VALUE_PERCENT,
    PLUGIN_UI_VALUE_TOGGLE,
    PLUGIN_UI_VALUE_DURATION,
    PLUGIN_UI_VALUE_THEME,
    PLUGIN_UI_VALUE_COUNT,
} plugin_ui_value_kind_t;

typedef enum {
    PLUGIN_EVENT_DATA_TYPE = 0,
    PLUGIN_EVENT_DATA_ID,
    PLUGIN_EVENT_DATA_HANDLE,
    PLUGIN_EVENT_DATA_VALUE,
    PLUGIN_EVENT_DATA_COUNT,
} plugin_event_data_t;

typedef struct {
    void *context;
    bool (*ui_clear)(void *context, uint32_t color);
    bool (*ui_title)(void *context, const char *title);
    bool (*ui_text)(void *context, int16_t x, int16_t y, uint8_t font,
                    plugin_vm_align_t align, uint32_t color, const char *text);
    bool (*ui_state)(void *context, int16_t x, int16_t y, uint8_t font,
                     plugin_vm_align_t align, uint32_t color, const char *prefix,
                     int32_t value);
    bool (*ui_rect)(void *context, int16_t x, int16_t y, int16_t width,
                    int16_t height, uint32_t color);
    bool (*ui_commit)(void *context);
    bool (*ui_screen)(void *context, const char *title);
    bool (*ui_value_card)(void *context, const char *label,
                          plugin_ui_value_kind_t kind, int32_t value,
                          const char *suffix);
    bool (*ui_list_row)(void *context, uint8_t row_id, const char *icon,
                        const char *label, plugin_ui_value_kind_t kind,
                        const char *text_value, int32_t value, bool selected,
                        bool enabled);
    bool (*ui_action_bar)(void *context, const char *navigation,
                          const char *ok, const char *back);
    bool (*ui_dialog_confirm)(void *context, uint16_t dialog_id,
                              const char *title, const char *message,
                              const char *cancel, const char *confirm);
    bool (*theme_next)(void *context, int32_t *index);
    bool (*theme_color)(void *context, uint8_t token, int32_t *color);
    bool (*device_info)(void *context);
    bool (*tone)(void *context, uint16_t frequency_hz, uint16_t duration_ms);
    bool (*kv_load)(void *context, const char *key, int32_t fallback, int32_t *value);
    bool (*kv_save)(void *context, const char *key, int32_t value);
    bool (*timer_set)(void *context, uint8_t timer_id, uint32_t delay_ms, bool repeat);
    bool (*setting_get)(void *context, uint8_t setting_id, int32_t *value);
    bool (*setting_set)(void *context, uint8_t setting_id, int32_t value);
    bool (*buffer_alloc)(void *context, uint16_t capacity, int32_t *handle);
    bool (*buffer_release)(void *context, int32_t handle);
    bool (*buffer_length)(void *context, int32_t handle, int32_t *length);
    bool (*buffer_read_u8)(void *context, int32_t handle, int32_t index,
                           int32_t *value);
    bool (*buffer_write_u8)(void *context, int32_t handle, int32_t index,
                            int32_t value);
    bool (*buffer_append_text)(void *context, int32_t handle, const char *text);
    bool (*nearby_acquire)(void *context);
    bool (*nearby_release)(void *context);
    bool (*nearby_send)(void *context, int32_t handle, int32_t *message_id);
    bool (*nearby_blob_accept)(void *context, int32_t transfer_id);
    bool (*nearby_blob_reject)(void *context, int32_t transfer_id);
    bool (*nearby_blob_send)(void *context, int32_t handle, const char *name,
                             const char *mime, int32_t *transfer_id);
    bool (*nearby_voice_start)(void *context);
    bool (*nearby_voice_transmit)(void *context, bool enabled);
    bool (*nearby_voice_stop)(void *context);
    void (*request_exit)(void *context);
} plugin_vm_host_t;

typedef struct {
    plugin_manifest_t manifest;
    const uint8_t *code;
    const uint8_t *strings;
    int32_t state[PLUGIN_STATE_SLOTS_MAX];
    int32_t event_data[PLUGIN_EVENT_DATA_COUNT];
    plugin_vm_host_t host;
    bool initialized;
} plugin_vm_t;

plugin_vm_result_t plugin_vm_init(plugin_vm_t *vm, const uint8_t *content,
                                  size_t content_size, const plugin_vm_host_t *host);
plugin_vm_result_t plugin_vm_dispatch(plugin_vm_t *vm, plugin_event_t event);
plugin_vm_result_t plugin_vm_dispatch_event(plugin_vm_t *vm, plugin_event_t event,
                                            int32_t type, int32_t id,
                                            int32_t handle, int32_t value);
plugin_vm_result_t plugin_vm_dispatch_data(plugin_vm_t *vm, plugin_event_t event,
                                           int32_t id, int32_t event_value);
const char *plugin_vm_result_name(plugin_vm_result_t result);
