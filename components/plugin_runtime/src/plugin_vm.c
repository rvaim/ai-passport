#include "plugin_vm.h"

#include "plugin_theme.h"

#include <limits.h>
#include <string.h>

enum {
    OP_END = 0x00,
    OP_PUSH_I32 = 0x01,
    OP_LOAD_STATE = 0x02,
    OP_STORE_STATE = 0x03,
    OP_ADD = 0x04,
    OP_SUB = 0x05,
    OP_MUL = 0x06,
    OP_DIV = 0x07,
    OP_MOD = 0x08,
    OP_EQ = 0x09,
    OP_LT = 0x0a,
    OP_GT = 0x0b,
    OP_NOT = 0x0c,
    OP_DUP = 0x0d,
    OP_DROP = 0x0e,
    OP_JUMP = 0x10,
    OP_JZ = 0x11,
    OP_JNZ = 0x12,
    OP_EVENT_LOAD = 0x13,
    OP_UI_CLEAR = 0x20,
    OP_UI_TITLE = 0x21,
    OP_UI_TEXT = 0x22,
    OP_UI_STATE = 0x23,
    OP_UI_RECT = 0x24,
    OP_UI_COMMIT = 0x25,
    OP_DEVICE_INFO = 0x26,
    OP_TONE = 0x30,
    OP_KV_LOAD = 0x31,
    OP_KV_SAVE = 0x32,
    OP_TIMER_SET = 0x33,
    OP_EXIT = 0x34,
    OP_SETTING_LOAD = 0x35,
    OP_SETTING_SAVE = 0x36,
    OP_UI_SCREEN = 0x40,
    OP_UI_VALUE_CARD = 0x41,
    OP_UI_LIST_ROW = 0x42,
    OP_UI_ACTION_BAR = 0x43,
    OP_UI_DIALOG_CONFIRM = 0x44,
    OP_THEME_NEXT = 0x45,
    OP_THEME_COLOR = 0x46,
    OP_BUFFER_ALLOC = 0x50,
    OP_BUFFER_RELEASE = 0x51,
    OP_BUFFER_LENGTH = 0x52,
    OP_BUFFER_READ_U8 = 0x53,
    OP_BUFFER_WRITE_U8 = 0x54,
    OP_BUFFER_APPEND_TEXT = 0x55,
    OP_NEARBY_ACQUIRE = 0x58,
    OP_NEARBY_RELEASE = 0x59,
    OP_NEARBY_SEND = 0x5a,
    OP_NEARBY_BLOB_ACCEPT = 0x5b,
    OP_NEARBY_BLOB_REJECT = 0x5c,
    OP_NEARBY_BLOB_SEND = 0x5d,
    OP_NEARBY_VOICE_START = 0x5e,
    OP_NEARBY_VOICE_TRANSMIT = 0x5f,
    OP_NEARBY_VOICE_STOP = 0x60,
};

typedef struct {
    plugin_vm_t *vm;
    const uint8_t *pc;
    const uint8_t *end;
    int32_t stack[PLUGIN_VM_STACK_SIZE];
    size_t depth;
} execution_t;

static bool take(execution_t *execution, size_t size, const uint8_t **value)
{
    if ((size_t)(execution->end - execution->pc) < size) {
        return false;
    }
    *value = execution->pc;
    execution->pc += size;
    return true;
}

static plugin_vm_result_t push(execution_t *execution, int32_t value)
{
    if (execution->depth >= PLUGIN_VM_STACK_SIZE) {
        return PLUGIN_VM_STACK_OVERFLOW;
    }
    execution->stack[execution->depth++] = value;
    return PLUGIN_VM_OK;
}

static plugin_vm_result_t pop(execution_t *execution, int32_t *value)
{
    if (execution->depth == 0U) {
        return PLUGIN_VM_STACK_UNDERFLOW;
    }
    *value = execution->stack[--execution->depth];
    return PLUGIN_VM_OK;
}

static plugin_vm_result_t binary(execution_t *execution, uint8_t opcode)
{
    int32_t left;
    int32_t right;
    plugin_vm_result_t result = pop(execution, &right);

    if (result != PLUGIN_VM_OK) {
        return result;
    }
    result = pop(execution, &left);
    if (result != PLUGIN_VM_OK) {
        return result;
    }
    switch (opcode) {
    case OP_ADD: {
        uint32_t bits = (uint32_t)left + (uint32_t)right;
        int32_t value;
        memcpy(&value, &bits, sizeof(value));
        return push(execution, value);
    }
    case OP_SUB: {
        uint32_t bits = (uint32_t)left - (uint32_t)right;
        int32_t value;
        memcpy(&value, &bits, sizeof(value));
        return push(execution, value);
    }
    case OP_MUL: {
        uint32_t bits = (uint32_t)left * (uint32_t)right;
        int32_t value;
        memcpy(&value, &bits, sizeof(value));
        return push(execution, value);
    }
    case OP_DIV:
        if (right == 0) {
            return PLUGIN_VM_DIVIDE_BY_ZERO;
        }
        return push(execution, left == INT32_MIN && right == -1 ? INT32_MIN : left / right);
    case OP_MOD:
        if (right == 0) {
            return PLUGIN_VM_DIVIDE_BY_ZERO;
        }
        return push(execution, left == INT32_MIN && right == -1 ? 0 : left % right);
    case OP_EQ:
        return push(execution, left == right);
    case OP_LT:
        return push(execution, left < right);
    case OP_GT:
        return push(execution, left > right);
    default:
        return PLUGIN_VM_BAD_OPCODE;
    }
}

static plugin_vm_result_t jump(execution_t *execution, int16_t relative)
{
    const uint8_t *start = execution->vm->code;
    int64_t current = execution->pc - start;
    int64_t code_size = execution->end - start;
    int64_t target = current + relative;

    if (target < 0 || target >= code_size) {
        return PLUGIN_VM_BAD_JUMP;
    }
    execution->pc = start + target;
    return PLUGIN_VM_OK;
}

static const char *string_operand(execution_t *execution, const uint8_t *data)
{
    return plugin_format_string_at(execution->vm->strings,
                                   execution->vm->manifest.strings_size,
                                   plugin_format_read_u16(data));
}

plugin_vm_result_t plugin_vm_init(plugin_vm_t *vm, const uint8_t *content,
                                  size_t content_size, const plugin_vm_host_t *host)
{
    plugin_format_result_t format_result;

    if (!vm || !content || !host) {
        return PLUGIN_VM_INVALID_ARGUMENT;
    }
    memset(vm, 0, sizeof(*vm));
    format_result = plugin_format_validate_content(content, content_size, &vm->manifest);
    if (format_result != PLUGIN_FORMAT_OK || vm->manifest.kind != PLUGIN_KIND_APP) {
        return PLUGIN_VM_INVALID_ARGUMENT;
    }
    vm->code = content + PLUGIN_MANIFEST_SIZE;
    vm->strings = vm->code + vm->manifest.code_size;
    vm->host = *host;
    vm->initialized = true;
    return PLUGIN_VM_OK;
}

plugin_vm_result_t plugin_vm_dispatch_event(plugin_vm_t *vm, plugin_event_t event,
                                            int32_t type, int32_t id,
                                            int32_t handle, int32_t event_value)
{
    execution_t execution;
    uint32_t handler;

    if (!vm || !vm->initialized || event >= PLUGIN_EVENT_COUNT) {
        return PLUGIN_VM_INVALID_ARGUMENT;
    }
    handler = vm->manifest.handlers[event];
    if (handler == PLUGIN_HANDLER_NONE) {
        return PLUGIN_VM_NO_HANDLER;
    }
    execution = (execution_t) {
        .vm = vm,
        .pc = vm->code + handler,
        .end = vm->code + vm->manifest.code_size,
    };
    vm->event_data[PLUGIN_EVENT_DATA_TYPE] = type;
    vm->event_data[PLUGIN_EVENT_DATA_ID] = id;
    vm->event_data[PLUGIN_EVENT_DATA_HANDLE] = handle;
    vm->event_data[PLUGIN_EVENT_DATA_VALUE] = event_value;

    for (size_t count = 0; count < PLUGIN_VM_INSTRUCTION_BUDGET; ++count) {
        const uint8_t *operand;
        uint8_t opcode;
        plugin_vm_result_t result;
        int32_t value;

        if (!take(&execution, 1U, &operand)) {
            return PLUGIN_VM_TRUNCATED;
        }
        opcode = operand[0];
        if (opcode == OP_END) {
            return PLUGIN_VM_OK;
        }
        if (opcode == OP_PUSH_I32) {
            if (!take(&execution, 4U, &operand)) {
                return PLUGIN_VM_TRUNCATED;
            }
            result = push(&execution, plugin_format_read_i32(operand));
            if (result != PLUGIN_VM_OK) {
                return result;
            }
        } else if (opcode == OP_LOAD_STATE || opcode == OP_STORE_STATE) {
            if (!take(&execution, 1U, &operand)) {
                return PLUGIN_VM_TRUNCATED;
            }
            if (operand[0] >= vm->manifest.state_slots) {
                return PLUGIN_VM_BAD_STATE_SLOT;
            }
            if (opcode == OP_LOAD_STATE) {
                result = push(&execution, vm->state[operand[0]]);
                if (result != PLUGIN_VM_OK) {
                    return result;
                }
            } else {
                result = pop(&execution, &value);
                if (result != PLUGIN_VM_OK) {
                    return result;
                }
                vm->state[operand[0]] = value;
            }
        } else if (opcode >= OP_ADD && opcode <= OP_GT) {
            result = binary(&execution, opcode);
            if (result != PLUGIN_VM_OK) {
                return result;
            }
        } else if (opcode == OP_NOT) {
            result = pop(&execution, &value);
            if (result != PLUGIN_VM_OK) {
                return result;
            }
            result = push(&execution, !value);
            if (result != PLUGIN_VM_OK) {
                return result;
            }
        } else if (opcode == OP_DUP) {
            if (execution.depth == 0U) {
                return PLUGIN_VM_STACK_UNDERFLOW;
            }
            result = push(&execution, execution.stack[execution.depth - 1U]);
            if (result != PLUGIN_VM_OK) {
                return result;
            }
        } else if (opcode == OP_DROP) {
            result = pop(&execution, &value);
            if (result != PLUGIN_VM_OK) {
                return result;
            }
        } else if (opcode == OP_JUMP || opcode == OP_JZ || opcode == OP_JNZ) {
            if (!take(&execution, 2U, &operand)) {
                return PLUGIN_VM_TRUNCATED;
            }
            int16_t relative = plugin_format_read_i16(operand);
            bool should_jump = opcode == OP_JUMP;
            if (opcode != OP_JUMP) {
                result = pop(&execution, &value);
                if (result != PLUGIN_VM_OK) {
                    return result;
                }
                should_jump = opcode == OP_JZ ? value == 0 : value != 0;
            }
            if (should_jump) {
                result = jump(&execution, relative);
                if (result != PLUGIN_VM_OK) {
                    return result;
                }
            }
        } else if (opcode == OP_EVENT_LOAD) {
            if (!take(&execution, 2U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (operand[0] >= PLUGIN_EVENT_DATA_COUNT ||
                operand[1] >= vm->manifest.state_slots) {
                return PLUGIN_VM_INVALID_ARGUMENT;
            }
            vm->state[operand[1]] = vm->event_data[operand[0]];
        } else if (opcode == OP_UI_CLEAR) {
            if (!take(&execution, 4U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (!vm->host.ui_clear || !vm->host.ui_clear(vm->host.context,
                                                         plugin_format_read_u32(operand)))
                return PLUGIN_VM_HOST_ERROR;
        } else if (opcode == OP_UI_TITLE) {
            if (!take(&execution, 2U, &operand)) return PLUGIN_VM_TRUNCATED;
            const char *text = string_operand(&execution, operand);
            if (!text) return PLUGIN_VM_BAD_STRING;
            if (!vm->host.ui_title || !vm->host.ui_title(vm->host.context, text))
                return PLUGIN_VM_HOST_ERROR;
        } else if (opcode == OP_UI_TEXT || opcode == OP_UI_STATE) {
            size_t operand_size = opcode == OP_UI_TEXT ? 12U : 13U;
            if (!take(&execution, operand_size, &operand)) return PLUGIN_VM_TRUNCATED;
            const char *text = string_operand(&execution, operand + 10U);
            if (!text) return PLUGIN_VM_BAD_STRING;
            if (opcode == OP_UI_TEXT) {
                if (!vm->host.ui_text ||
                    !vm->host.ui_text(vm->host.context,
                                      plugin_format_read_i16(operand),
                                      plugin_format_read_i16(operand + 2U),
                                      operand[4], (plugin_vm_align_t)operand[5],
                                      plugin_format_read_u32(operand + 6U), text))
                    return PLUGIN_VM_HOST_ERROR;
            } else {
                uint8_t slot = operand[12];
                if (slot >= vm->manifest.state_slots) return PLUGIN_VM_BAD_STATE_SLOT;
                if (!vm->host.ui_state ||
                    !vm->host.ui_state(vm->host.context,
                                       plugin_format_read_i16(operand),
                                       plugin_format_read_i16(operand + 2U),
                                       operand[4], (plugin_vm_align_t)operand[5],
                                       plugin_format_read_u32(operand + 6U), text,
                                       vm->state[slot]))
                    return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_UI_RECT) {
            if (!take(&execution, 12U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (!vm->host.ui_rect ||
                !vm->host.ui_rect(vm->host.context,
                                  plugin_format_read_i16(operand),
                                  plugin_format_read_i16(operand + 2U),
                                  plugin_format_read_i16(operand + 4U),
                                  plugin_format_read_i16(operand + 6U),
                                  plugin_format_read_u32(operand + 8U)))
                return PLUGIN_VM_HOST_ERROR;
        } else if (opcode == OP_UI_COMMIT) {
            if (!vm->host.ui_commit || !vm->host.ui_commit(vm->host.context))
                return PLUGIN_VM_HOST_ERROR;
        } else if (opcode == OP_UI_SCREEN) {
            if (!take(&execution, 2U, &operand)) return PLUGIN_VM_TRUNCATED;
            const char *title = string_operand(&execution, operand);
            if (!title) return PLUGIN_VM_BAD_STRING;
            if (!vm->host.ui_screen || !vm->host.ui_screen(vm->host.context, title)) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_UI_VALUE_CARD) {
            if (!take(&execution, 6U, &operand)) return PLUGIN_VM_TRUNCATED;
            const char *label = string_operand(&execution, operand);
            plugin_ui_value_kind_t kind = (plugin_ui_value_kind_t)operand[2];
            uint8_t slot = operand[3];
            const char *suffix = string_operand(&execution, operand + 4U);
            if (!label || !suffix) return PLUGIN_VM_BAD_STRING;
            if (kind <= PLUGIN_UI_VALUE_TEXT || kind >= PLUGIN_UI_VALUE_COUNT ||
                slot >= vm->manifest.state_slots) {
                return PLUGIN_VM_INVALID_ARGUMENT;
            }
            if (!vm->host.ui_value_card ||
                !vm->host.ui_value_card(vm->host.context, label, kind,
                                        vm->state[slot], suffix)) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_UI_LIST_ROW) {
            if (!take(&execution, 11U, &operand)) return PLUGIN_VM_TRUNCATED;
            uint8_t row_id = operand[0];
            plugin_ui_value_kind_t kind = (plugin_ui_value_kind_t)operand[1];
            uint8_t value_slot = operand[2];
            uint8_t selected_slot = operand[3];
            const char *icon = string_operand(&execution, operand + 5U);
            const char *label = string_operand(&execution, operand + 7U);
            const char *text_value = string_operand(&execution, operand + 9U);
            if (!icon || !label || !text_value) return PLUGIN_VM_BAD_STRING;
            if (row_id >= 8U || kind >= PLUGIN_UI_VALUE_COUNT ||
                selected_slot >= vm->manifest.state_slots ||
                (kind != PLUGIN_UI_VALUE_NONE && kind != PLUGIN_UI_VALUE_TEXT &&
                 value_slot >= vm->manifest.state_slots)) {
                return PLUGIN_VM_INVALID_ARGUMENT;
            }
            int32_t row_value = value_slot < vm->manifest.state_slots
                ? vm->state[value_slot] : 0;
            if (!vm->host.ui_list_row ||
                !vm->host.ui_list_row(vm->host.context, row_id, icon, label, kind,
                                      text_value, row_value,
                                      vm->state[selected_slot] == row_id,
                                      operand[4] != 0U)) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_UI_ACTION_BAR) {
            if (!take(&execution, 6U, &operand)) return PLUGIN_VM_TRUNCATED;
            const char *navigation = string_operand(&execution, operand);
            const char *ok = string_operand(&execution, operand + 2U);
            const char *back = string_operand(&execution, operand + 4U);
            if (!navigation || !ok || !back) return PLUGIN_VM_BAD_STRING;
            if (!vm->host.ui_action_bar ||
                !vm->host.ui_action_bar(vm->host.context, navigation, ok, back)) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_UI_DIALOG_CONFIRM) {
            if (!take(&execution, 10U, &operand)) return PLUGIN_VM_TRUNCATED;
            const char *title = string_operand(&execution, operand + 2U);
            const char *message = string_operand(&execution, operand + 4U);
            const char *cancel = string_operand(&execution, operand + 6U);
            const char *confirm = string_operand(&execution, operand + 8U);
            if (!title || !message || !cancel || !confirm) {
                return PLUGIN_VM_BAD_STRING;
            }
            if (!vm->host.ui_dialog_confirm ||
                !vm->host.ui_dialog_confirm(vm->host.context,
                                            plugin_format_read_u16(operand),
                                            title, message, cancel, confirm)) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_THEME_NEXT) {
            if (!(vm->manifest.permissions & PLUGIN_PERMISSION_SETTINGS)) {
                return PLUGIN_VM_PERMISSION_DENIED;
            }
            if (!take(&execution, 1U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (operand[0] >= vm->manifest.state_slots || !vm->host.theme_next ||
                !vm->host.theme_next(vm->host.context, &vm->state[operand[0]])) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_THEME_COLOR) {
            if (!take(&execution, 2U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (operand[0] >= PLUGIN_THEME_COLOR_COUNT ||
                operand[1] >= vm->manifest.state_slots || !vm->host.theme_color ||
                !vm->host.theme_color(vm->host.context, operand[0],
                                      &vm->state[operand[1]])) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_DEVICE_INFO) {
            if (!(vm->manifest.permissions & PLUGIN_PERMISSION_SETTINGS))
                return PLUGIN_VM_PERMISSION_DENIED;
            if (!vm->host.device_info || !vm->host.device_info(vm->host.context))
                return PLUGIN_VM_HOST_ERROR;
        } else if (opcode == OP_TONE) {
            if (!(vm->manifest.permissions & PLUGIN_PERMISSION_AUDIO))
                return PLUGIN_VM_PERMISSION_DENIED;
            if (!take(&execution, 4U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (!vm->host.tone ||
                !vm->host.tone(vm->host.context, plugin_format_read_u16(operand),
                               plugin_format_read_u16(operand + 2U)))
                return PLUGIN_VM_HOST_ERROR;
        } else if (opcode == OP_KV_LOAD || opcode == OP_KV_SAVE) {
            size_t operand_size = opcode == OP_KV_LOAD ? 7U : 3U;
            if (!(vm->manifest.permissions & PLUGIN_PERMISSION_STORAGE))
                return PLUGIN_VM_PERMISSION_DENIED;
            if (!take(&execution, operand_size, &operand)) return PLUGIN_VM_TRUNCATED;
            uint8_t slot = operand[0];
            const char *key = string_operand(&execution, operand + 1U);
            if (slot >= vm->manifest.state_slots) return PLUGIN_VM_BAD_STATE_SLOT;
            if (!key) return PLUGIN_VM_BAD_STRING;
            if (opcode == OP_KV_LOAD) {
                int32_t fallback = plugin_format_read_i32(operand + 3U);
                if (!vm->host.kv_load ||
                    !vm->host.kv_load(vm->host.context, key, fallback, &vm->state[slot]))
                    return PLUGIN_VM_HOST_ERROR;
            } else if (!vm->host.kv_save ||
                       !vm->host.kv_save(vm->host.context, key, vm->state[slot])) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_TIMER_SET) {
            if (!take(&execution, 6U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (operand[0] >= 4U || !vm->host.timer_set ||
                !vm->host.timer_set(vm->host.context, operand[0],
                                    plugin_format_read_u32(operand + 1U), operand[5] != 0U))
                return PLUGIN_VM_HOST_ERROR;
        } else if (opcode == OP_SETTING_LOAD || opcode == OP_SETTING_SAVE) {
            if (!(vm->manifest.permissions & PLUGIN_PERMISSION_SETTINGS))
                return PLUGIN_VM_PERMISSION_DENIED;
            if (!take(&execution, 2U, &operand)) return PLUGIN_VM_TRUNCATED;
            uint8_t setting_id = operand[0];
            uint8_t slot = operand[1];
            if (setting_id >= PLUGIN_SETTING_COUNT ||
                slot >= vm->manifest.state_slots) {
                return PLUGIN_VM_INVALID_ARGUMENT;
            }
            if (opcode == OP_SETTING_LOAD) {
                if (!vm->host.setting_get ||
                    !vm->host.setting_get(vm->host.context, setting_id,
                                          &vm->state[slot])) {
                    return PLUGIN_VM_HOST_ERROR;
                }
            } else if (!vm->host.setting_set ||
                       !vm->host.setting_set(vm->host.context, setting_id,
                                             vm->state[slot])) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_BUFFER_ALLOC) {
            if (!take(&execution, 3U, &operand)) return PLUGIN_VM_TRUNCATED;
            uint8_t destination = operand[2];
            if (destination >= vm->manifest.state_slots) {
                return PLUGIN_VM_BAD_STATE_SLOT;
            }
            if (!vm->host.buffer_alloc ||
                !vm->host.buffer_alloc(vm->host.context,
                                       plugin_format_read_u16(operand),
                                       &vm->state[destination])) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_BUFFER_RELEASE) {
            if (!take(&execution, 1U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (operand[0] >= vm->manifest.state_slots) {
                return PLUGIN_VM_BAD_STATE_SLOT;
            }
            if (!vm->host.buffer_release ||
                !vm->host.buffer_release(vm->host.context,
                                         vm->state[operand[0]])) {
                return PLUGIN_VM_HOST_ERROR;
            }
            vm->state[operand[0]] = 0;
        } else if (opcode == OP_BUFFER_LENGTH) {
            if (!take(&execution, 2U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (operand[0] >= vm->manifest.state_slots ||
                operand[1] >= vm->manifest.state_slots) {
                return PLUGIN_VM_BAD_STATE_SLOT;
            }
            if (!vm->host.buffer_length ||
                !vm->host.buffer_length(vm->host.context,
                                        vm->state[operand[0]],
                                        &vm->state[operand[1]])) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_BUFFER_READ_U8) {
            if (!take(&execution, 3U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (operand[0] >= vm->manifest.state_slots ||
                operand[1] >= vm->manifest.state_slots ||
                operand[2] >= vm->manifest.state_slots) {
                return PLUGIN_VM_BAD_STATE_SLOT;
            }
            if (!vm->host.buffer_read_u8 ||
                !vm->host.buffer_read_u8(vm->host.context,
                                         vm->state[operand[0]],
                                         vm->state[operand[1]],
                                         &vm->state[operand[2]])) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_BUFFER_WRITE_U8) {
            if (!take(&execution, 3U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (operand[0] >= vm->manifest.state_slots ||
                operand[1] >= vm->manifest.state_slots ||
                operand[2] >= vm->manifest.state_slots) {
                return PLUGIN_VM_BAD_STATE_SLOT;
            }
            if (!vm->host.buffer_write_u8 ||
                !vm->host.buffer_write_u8(vm->host.context,
                                          vm->state[operand[0]],
                                          vm->state[operand[1]],
                                          vm->state[operand[2]])) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_BUFFER_APPEND_TEXT) {
            if (!take(&execution, 3U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (operand[0] >= vm->manifest.state_slots) {
                return PLUGIN_VM_BAD_STATE_SLOT;
            }
            const char *text = string_operand(&execution, operand + 1U);
            if (!text) return PLUGIN_VM_BAD_STRING;
            if (!vm->host.buffer_append_text ||
                !vm->host.buffer_append_text(vm->host.context,
                                             vm->state[operand[0]], text)) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_NEARBY_ACQUIRE || opcode == OP_NEARBY_RELEASE) {
            if (!(vm->manifest.permissions & PLUGIN_PERMISSION_NEARBY)) {
                return PLUGIN_VM_PERMISSION_DENIED;
            }
            bool ok = opcode == OP_NEARBY_ACQUIRE
                ? vm->host.nearby_acquire &&
                  vm->host.nearby_acquire(vm->host.context)
                : vm->host.nearby_release &&
                  vm->host.nearby_release(vm->host.context);
            if (!ok) return PLUGIN_VM_HOST_ERROR;
        } else if (opcode == OP_NEARBY_SEND) {
            if (!(vm->manifest.permissions & PLUGIN_PERMISSION_NEARBY)) {
                return PLUGIN_VM_PERMISSION_DENIED;
            }
            if (!take(&execution, 2U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (operand[0] >= vm->manifest.state_slots ||
                operand[1] >= vm->manifest.state_slots) {
                return PLUGIN_VM_BAD_STATE_SLOT;
            }
            if (!vm->host.nearby_send ||
                !vm->host.nearby_send(vm->host.context,
                                      vm->state[operand[0]],
                                      &vm->state[operand[1]])) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_NEARBY_BLOB_ACCEPT ||
                   opcode == OP_NEARBY_BLOB_REJECT) {
            if (!(vm->manifest.permissions & PLUGIN_PERMISSION_NEARBY)) {
                return PLUGIN_VM_PERMISSION_DENIED;
            }
            if (!take(&execution, 1U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (operand[0] >= vm->manifest.state_slots) {
                return PLUGIN_VM_BAD_STATE_SLOT;
            }
            bool ok = opcode == OP_NEARBY_BLOB_ACCEPT
                ? vm->host.nearby_blob_accept &&
                  vm->host.nearby_blob_accept(vm->host.context,
                                              vm->state[operand[0]])
                : vm->host.nearby_blob_reject &&
                  vm->host.nearby_blob_reject(vm->host.context,
                                              vm->state[operand[0]]);
            if (!ok) return PLUGIN_VM_HOST_ERROR;
        } else if (opcode == OP_NEARBY_BLOB_SEND) {
            if (!(vm->manifest.permissions & PLUGIN_PERMISSION_NEARBY)) {
                return PLUGIN_VM_PERMISSION_DENIED;
            }
            if (!take(&execution, 6U, &operand)) return PLUGIN_VM_TRUNCATED;
            if (operand[0] >= vm->manifest.state_slots ||
                operand[5] >= vm->manifest.state_slots) {
                return PLUGIN_VM_BAD_STATE_SLOT;
            }
            const char *name = string_operand(&execution, operand + 1U);
            const char *mime = string_operand(&execution, operand + 3U);
            if (!name || !mime) return PLUGIN_VM_BAD_STRING;
            if (!vm->host.nearby_blob_send ||
                !vm->host.nearby_blob_send(vm->host.context,
                                           vm->state[operand[0]], name, mime,
                                           &vm->state[operand[5]])) {
                return PLUGIN_VM_HOST_ERROR;
            }
        } else if (opcode == OP_NEARBY_VOICE_START ||
                   opcode == OP_NEARBY_VOICE_TRANSMIT ||
                   opcode == OP_NEARBY_VOICE_STOP) {
            const uint32_t voice_permissions = PLUGIN_PERMISSION_NEARBY |
                                               PLUGIN_PERMISSION_AUDIO |
                                               PLUGIN_PERMISSION_MICROPHONE;
            if ((vm->manifest.permissions & voice_permissions) != voice_permissions) {
                return PLUGIN_VM_PERMISSION_DENIED;
            }
            bool ok;
            if (opcode == OP_NEARBY_VOICE_START) {
                ok = vm->host.nearby_voice_start &&
                     vm->host.nearby_voice_start(vm->host.context);
            } else if (opcode == OP_NEARBY_VOICE_STOP) {
                ok = vm->host.nearby_voice_stop &&
                     vm->host.nearby_voice_stop(vm->host.context);
            } else {
                if (!take(&execution, 1U, &operand)) return PLUGIN_VM_TRUNCATED;
                if (operand[0] >= vm->manifest.state_slots) {
                    return PLUGIN_VM_BAD_STATE_SLOT;
                }
                ok = vm->host.nearby_voice_transmit &&
                     vm->host.nearby_voice_transmit(vm->host.context,
                                                    vm->state[operand[0]] != 0);
            }
            if (!ok) return PLUGIN_VM_HOST_ERROR;
        } else if (opcode == OP_EXIT) {
            if (vm->host.request_exit) vm->host.request_exit(vm->host.context);
        } else {
            return PLUGIN_VM_BAD_OPCODE;
        }
    }
    return PLUGIN_VM_BUDGET_EXCEEDED;
}

plugin_vm_result_t plugin_vm_dispatch(plugin_vm_t *vm, plugin_event_t event)
{
    return plugin_vm_dispatch_event(vm, event, 0, 0, 0, 0);
}

plugin_vm_result_t plugin_vm_dispatch_data(plugin_vm_t *vm, plugin_event_t event,
                                           int32_t id, int32_t event_value)
{
    return plugin_vm_dispatch_event(vm, event, 0, id, 0, event_value);
}

const char *plugin_vm_result_name(plugin_vm_result_t result)
{
    static const char *const names[] = {
        "ok", "invalid argument", "no handler", "bad opcode", "truncated",
        "stack overflow", "stack underflow", "bad state slot", "bad string",
        "bad jump", "divide by zero", "budget exceeded", "permission denied",
        "host error",
    };
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "unknown";
}
