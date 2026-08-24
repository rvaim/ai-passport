#include "plugin_vm.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int32_t released[8];
    size_t released_count;
    int32_t accepted_id;
    unsigned acquires;
    unsigned releases;
    unsigned sends;
    unsigned exits;
    unsigned commits;
} host_context_t;

static bool ui_screen(void *context, const char *title)
{
    (void)context;
    return strcmp(title, "近场通信") == 0;
}

static bool ui_value_card(void *context, const char *label,
                          plugin_ui_value_kind_t kind, int32_t value,
                          const char *suffix)
{
    (void)context;
    return strcmp(label, "连接状态") == 0 &&
           kind == PLUGIN_UI_VALUE_INTEGER && value >= 0 &&
           strcmp(suffix, "") == 0;
}

static bool ui_list_row(void *context, uint8_t row_id, const char *icon,
                        const char *label, plugin_ui_value_kind_t kind,
                        const char *text_value, int32_t value, bool selected,
                        bool enabled)
{
    (void)context;
    (void)value;
    return row_id < 2U && icon && label && text_value &&
           kind == PLUGIN_UI_VALUE_INTEGER && enabled &&
           selected == (row_id == 0U);
}

static bool ui_action_bar(void *context, const char *navigation,
                          const char *ok, const char *back)
{
    (void)context;
    return strcmp(navigation, "") == 0 && strcmp(ok, "发送问候") == 0 &&
           strcmp(back, "返回") == 0;
}

static bool ui_commit(void *context)
{
    host_context_t *host = context;
    ++host->commits;
    return true;
}

static bool buffer_alloc(void *context, uint16_t capacity, int32_t *handle)
{
    (void)context;
    assert(capacity == 128U && handle);
    *handle = 77;
    return true;
}

static bool buffer_append_text(void *context, int32_t handle, const char *text)
{
    (void)context;
    return handle == 77 && strcmp(text, "来自 Passport 的问候") == 0;
}

static bool buffer_release(void *context, int32_t handle)
{
    host_context_t *host = context;
    assert(host->released_count < sizeof(host->released) / sizeof(host->released[0]));
    host->released[host->released_count++] = handle;
    return true;
}

static bool nearby_acquire(void *context)
{
    host_context_t *host = context;
    ++host->acquires;
    return true;
}

static bool nearby_release(void *context)
{
    host_context_t *host = context;
    ++host->releases;
    return true;
}

static bool nearby_send(void *context, int32_t handle, int32_t *message_id)
{
    host_context_t *host = context;
    assert(handle == 77 && message_id);
    *message_id = 42;
    ++host->sends;
    return true;
}

static bool nearby_blob_accept(void *context, int32_t transfer_id)
{
    host_context_t *host = context;
    host->accepted_id = transfer_id;
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
    assert(data && fread(data, 1U, (size_t)length, file) == (size_t)length);
    fclose(file);
    *size = (size_t)length;
    return data;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    size_t content_size;
    uint8_t *content = read_file(argv[1], &content_size);
    host_context_t context = {0};
    plugin_vm_host_t host = {
        .context = &context,
        .ui_screen = ui_screen,
        .ui_value_card = ui_value_card,
        .ui_list_row = ui_list_row,
        .ui_action_bar = ui_action_bar,
        .ui_commit = ui_commit,
        .buffer_alloc = buffer_alloc,
        .buffer_release = buffer_release,
        .buffer_append_text = buffer_append_text,
        .nearby_acquire = nearby_acquire,
        .nearby_release = nearby_release,
        .nearby_send = nearby_send,
        .nearby_blob_accept = nearby_blob_accept,
        .request_exit = request_exit,
    };
    plugin_vm_t vm;

    assert(plugin_vm_init(&vm, content, content_size, &host) == PLUGIN_VM_OK);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_START) == PLUGIN_VM_OK);
    assert(vm.state[5] == 77 && context.acquires == 1U && context.commits == 1U);

    assert(plugin_vm_dispatch_event(&vm, PLUGIN_EVENT_NEARBY, 1, 0, 0, 2) ==
           PLUGIN_VM_OK);
    assert(vm.state[0] == 2 && vm.state[1] == 1 && context.commits == 2U);

    assert(plugin_vm_dispatch_event(&vm, PLUGIN_EVENT_NEARBY, 2, 9, 88, 12) ==
           PLUGIN_VM_OK);
    assert(vm.state[2] == 9 && vm.state[3] == 0 && vm.state[4] == 12);
    assert(context.released[0] == 88);

    assert(plugin_vm_dispatch_event(&vm, PLUGIN_EVENT_NEARBY, 4, 55, 99, 1000) ==
           PLUGIN_VM_OK);
    assert(context.released[1] == 99 && context.accepted_id == 55);

    assert(plugin_vm_dispatch_event(&vm, PLUGIN_EVENT_NEARBY, 6, 55, 100, 1000) ==
           PLUGIN_VM_OK);
    assert(context.released[2] == 100);

    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(context.sends == 1U && vm.state[6] == 42);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_BACK) == PLUGIN_VM_OK);
    assert(context.releases == 1U && context.released[3] == 77 &&
           context.exits == 1U);

    free(content);
    puts("nearby demo plugin VM test passed");
    return 0;
}
