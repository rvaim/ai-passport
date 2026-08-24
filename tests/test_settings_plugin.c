#include "plugin_vm.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned screens;
    unsigned rows;
    unsigned commits;
    unsigned device_views;
    unsigned exits;
    unsigned theme_changes;
    int32_t settings[PLUGIN_SETTING_THEME];
    int32_t theme_index;
} host_context_t;

static bool ui_screen(void *context, const char *title)
{
    host_context_t *host = context;
    assert(strcmp(title, "设置") == 0);
    ++host->screens;
    return true;
}

static bool ui_list_row(void *context, uint8_t row_id, const char *icon,
                        const char *label, plugin_ui_value_kind_t kind,
                        const char *text_value, int32_t value, bool selected,
                        bool enabled)
{
    host_context_t *host = context;
    assert(row_id < 6U && icon && label && text_value && enabled);
    assert(kind < PLUGIN_UI_VALUE_COUNT);
    (void)selected;
    (void)value;
    ++host->rows;
    return true;
}

static bool ui_action_bar(void *context, const char *navigation,
                          const char *ok, const char *back)
{
    (void)context;
    assert(strcmp(navigation, "选择") == 0);
    assert(strcmp(ok, "修改") == 0);
    assert(strcmp(back, "返回") == 0);
    return true;
}

static bool ui_commit(void *context)
{
    host_context_t *host = context;
    ++host->commits;
    return true;
}

static bool device_info(void *context)
{
    host_context_t *host = context;
    ++host->device_views;
    return true;
}

static bool setting_get(void *context, uint8_t setting_id, int32_t *value)
{
    host_context_t *host = context;
    if (!value || setting_id >= PLUGIN_SETTING_COUNT) return false;
    *value = setting_id == PLUGIN_SETTING_THEME
        ? host->theme_index : host->settings[setting_id];
    return true;
}

static bool setting_set(void *context, uint8_t setting_id, int32_t value)
{
    host_context_t *host = context;
    if (setting_id >= PLUGIN_SETTING_THEME) return false;
    host->settings[setting_id] = value;
    return true;
}

static bool theme_next(void *context, int32_t *index)
{
    host_context_t *host = context;
    assert(index);
    host->theme_index = (host->theme_index + 1) % 2;
    *index = host->theme_index;
    ++host->theme_changes;
    return true;
}

static void request_exit(void *context)
{
    host_context_t *host = context;
    ++host->exits;
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
    host_context_t context = {
        .settings = {80, 50, 0, 0},
    };
    plugin_vm_host_t host = {
        .context = &context,
        .ui_screen = ui_screen,
        .ui_list_row = ui_list_row,
        .ui_action_bar = ui_action_bar,
        .ui_commit = ui_commit,
        .device_info = device_info,
        .setting_get = setting_get,
        .setting_set = setting_set,
        .theme_next = theme_next,
        .request_exit = request_exit,
    };
    plugin_vm_t vm;

    assert(plugin_vm_init(&vm, content, content_size, &host) == PLUGIN_VM_OK);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_START) == PLUGIN_VM_OK);
    assert(vm.state[0] == 0 && context.rows == 6U && context.commits == 1U);

    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_DOWN) == PLUGIN_VM_OK);
    assert(vm.state[0] == 1);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(context.settings[PLUGIN_SETTING_VOLUME] == 60);

    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_UP) == PLUGIN_VM_OK);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(context.settings[PLUGIN_SETTING_BRIGHTNESS] == 90);

    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_DOWN) == PLUGIN_VM_OK);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_DOWN) == PLUGIN_VM_OK);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(context.settings[PLUGIN_SETTING_KEY_SOUND] == 1);

    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_DOWN) == PLUGIN_VM_OK);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(context.settings[PLUGIN_SETTING_SCREEN_TIMEOUT] == 30);

    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_DOWN) == PLUGIN_VM_OK);
    assert(vm.state[0] == 4);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(context.theme_changes == 1U && vm.state[5] == 1);

    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_DOWN) == PLUGIN_VM_OK);
    assert(vm.state[0] == 5);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(vm.state[6] == 1 && context.device_views == 1U);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_BACK) == PLUGIN_VM_OK);
    assert(vm.state[6] == 0 && context.exits == 0U);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_BACK) == PLUGIN_VM_OK);
    assert(context.exits == 1U);
    assert(context.screens == context.commits && context.rows == context.commits * 6U);

    free(content);
    puts("settings plugin VM test passed");
    return 0;
}
