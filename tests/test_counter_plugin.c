#include "plugin_vm.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int32_t saved_count;
    int32_t card_value;
    uint16_t dialog_id;
    unsigned commits;
    unsigned saves;
    unsigned tones;
    unsigned dialogs;
} host_context_t;

static bool ui_screen(void *context, const char *title)
{
    (void)context;
    return strcmp(title, "计数器") == 0;
}

static bool ui_value_card(void *context, const char *label,
                          plugin_ui_value_kind_t kind, int32_t value,
                          const char *suffix)
{
    host_context_t *host = context;
    assert(strcmp(label, "当前计数") == 0);
    assert(kind == PLUGIN_UI_VALUE_INTEGER && strcmp(suffix, "") == 0);
    host->card_value = value;
    return true;
}

static bool ui_action_bar(void *context, const char *navigation,
                          const char *ok, const char *back)
{
    (void)context;
    return strcmp(navigation, "调整") == 0 && strcmp(ok, "清零") == 0 &&
           strcmp(back, "返回") == 0;
}

static bool ui_commit(void *context)
{
    host_context_t *host = context;
    ++host->commits;
    return true;
}

static bool ui_dialog_confirm(void *context, uint16_t dialog_id,
                              const char *title, const char *message,
                              const char *cancel, const char *confirm)
{
    host_context_t *host = context;
    assert(strcmp(title, "确认清零?") == 0);
    assert(message && cancel && confirm);
    host->dialog_id = dialog_id;
    ++host->dialogs;
    return true;
}

static bool tone(void *context, uint16_t frequency_hz, uint16_t duration_ms)
{
    host_context_t *host = context;
    assert(frequency_hz >= 20U && duration_ms > 0U);
    ++host->tones;
    return true;
}

static bool kv_load(void *context, const char *key, int32_t fallback, int32_t *value)
{
    host_context_t *host = context;
    assert(strcmp(key, "count") == 0 && value);
    *value = host->saved_count == INT32_MIN ? fallback : host->saved_count;
    return true;
}

static bool kv_save(void *context, const char *key, int32_t value)
{
    host_context_t *host = context;
    assert(strcmp(key, "count") == 0);
    host->saved_count = value;
    ++host->saves;
    return true;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0);
    rewind(file);
    uint8_t *data = malloc((size_t)length);
    assert(data);
    assert(fread(data, 1U, (size_t)length, file) == (size_t)length);
    fclose(file);
    *size = (size_t)length;
    return data;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    size_t content_size;
    uint8_t *content = read_file(argv[1], &content_size);
    host_context_t context = {.saved_count = 5};
    plugin_vm_host_t host = {
        .context = &context,
        .ui_screen = ui_screen,
        .ui_value_card = ui_value_card,
        .ui_action_bar = ui_action_bar,
        .ui_commit = ui_commit,
        .ui_dialog_confirm = ui_dialog_confirm,
        .tone = tone,
        .kv_load = kv_load,
        .kv_save = kv_save,
    };
    plugin_vm_t vm;

    assert(plugin_vm_init(&vm, content, content_size, &host) == PLUGIN_VM_OK);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_START) == PLUGIN_VM_OK);
    assert(vm.state[0] == 5 && context.card_value == 5 && context.commits == 1U);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_UP) == PLUGIN_VM_OK);
    assert(vm.state[0] == 6 && context.saved_count == 6 && context.tones == 1U);

    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(context.dialogs == 1U && context.dialog_id == 1U);
    assert(plugin_vm_dispatch_data(&vm, PLUGIN_EVENT_ACTION, 1, 0) == PLUGIN_VM_OK);
    assert(vm.state[0] == 6 && context.saves == 1U);
    assert(plugin_vm_dispatch_data(&vm, PLUGIN_EVENT_ACTION, 1, 1) == PLUGIN_VM_OK);
    assert(vm.state[0] == 0 && context.saved_count == 0 && context.card_value == 0);
    assert(vm.state[1] == 1 && vm.state[2] == 1 && context.saves == 2U);

    free(content);
    puts("counter plugin VM test passed");
    return 0;
}
