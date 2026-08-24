#include "plugin_vm.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned objects;
    unsigned max_objects;
    unsigned commits;
    unsigned tones;
    uint16_t last_frequency;
    unsigned timer_sets;
    unsigned kv_saves;
    int32_t saved_best;
    bool has_best;
} host_context_t;

static bool add_object(host_context_t *host)
{
    ++host->objects;
    if (host->objects > host->max_objects) host->max_objects = host->objects;
    return host->objects <= 24U;
}

static bool ui_clear(void *context, uint32_t color)
{
    host_context_t *host = context;
    (void)color;
    host->objects = 0U;
    return true;
}

static bool ui_title(void *context, const char *title)
{
    assert(strcmp(title, "流星射击") == 0);
    return add_object(context);
}

static bool ui_text(void *context, int16_t x, int16_t y, uint8_t font,
                    plugin_vm_align_t align, uint32_t color, const char *text)
{
    assert(x >= 0 && x <= 240 && y >= 0 && y < 320);
    assert(font <= 1U && align <= PLUGIN_VM_ALIGN_RIGHT);
    (void)color;
    assert(text);
    return add_object(context);
}

static bool ui_state(void *context, int16_t x, int16_t y, uint8_t font,
                     plugin_vm_align_t align, uint32_t color, const char *prefix,
                     int32_t value)
{
    assert(x >= 0 && x <= 240 && y >= 0 && y < 320);
    assert(font <= 1U && align <= PLUGIN_VM_ALIGN_RIGHT);
    (void)color;
    (void)value;
    assert(prefix);
    return add_object(context);
}

static bool ui_rect(void *context, int16_t x, int16_t y, int16_t width,
                    int16_t height, uint32_t color)
{
    (void)color;
    assert(x >= 0 && y >= 0 && width > 0 && height > 0);
    assert(x + width <= 240 && y + height <= 320);
    return add_object(context);
}

static bool ui_commit(void *context)
{
    host_context_t *host = context;
    ++host->commits;
    return host->objects <= 24U;
}

static bool ui_action_bar(void *context, const char *navigation,
                          const char *ok, const char *back)
{
    (void)context;
    assert(navigation && ok && back);
    return true;
}

static bool tone(void *context, uint16_t frequency_hz, uint16_t duration_ms)
{
    host_context_t *host = context;
    assert(duration_ms > 0U);
    ++host->tones;
    host->last_frequency = frequency_hz;
    return true;
}

static bool kv_load(void *context, const char *key, int32_t fallback, int32_t *value)
{
    host_context_t *host = context;
    assert(strcmp(key, "best") == 0 && value);
    *value = host->has_best ? host->saved_best : fallback;
    return true;
}

static bool kv_save(void *context, const char *key, int32_t value)
{
    host_context_t *host = context;
    assert(strcmp(key, "best") == 0);
    if (host->kv_saves >= 128U) return false;
    ++host->kv_saves;
    host->saved_best = value;
    host->has_best = true;
    return true;
}

static bool timer_set(void *context, uint8_t timer_id, uint32_t delay_ms, bool repeat)
{
    host_context_t *host = context;
    assert(timer_id == 0U && delay_ms == 450U && repeat);
    ++host->timer_sets;
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
    host_context_t context = {
        .saved_best = 7,
        .has_best = true,
    };
    plugin_vm_host_t host = {
        .context = &context,
        .ui_clear = ui_clear,
        .ui_title = ui_title,
        .ui_text = ui_text,
        .ui_state = ui_state,
        .ui_rect = ui_rect,
        .ui_commit = ui_commit,
        .ui_action_bar = ui_action_bar,
        .tone = tone,
        .kv_load = kv_load,
        .kv_save = kv_save,
        .timer_set = timer_set,
    };
    plugin_vm_t vm;

    assert(plugin_vm_init(&vm, content, content_size, &host) == PLUGIN_VM_OK);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_START) == PLUGIN_VM_OK);
    assert(vm.state[0] == 2 && vm.state[1] == 0 && vm.state[2] == 0);
    assert(vm.state[3] == 3 && vm.state[4] == 7 && vm.state[5] == 0);
    assert(vm.state[6] == 0);
    assert(context.timer_sets == 1U && context.commits == 1U);

    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_UP) == PLUGIN_VM_OK);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_UP) == PLUGIN_VM_OK);
    assert(vm.state[0] == 0);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(vm.state[2] == 1 && vm.state[3] == 3 && vm.state[1] == 2);
    assert(vm.state[4] == 7 && vm.state[5] == 1);
    assert(context.tones == 1U && context.last_frequency == 1320U);

    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_TIMER0) == PLUGIN_VM_OK);
    assert(vm.state[1] == 3 && vm.state[5] == 0);
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(vm.state[3] == 2 && vm.state[5] == -1);
    assert(context.tones == 2U && context.last_frequency == 220U);

    /* The fifth lane for both sprites is the render path with the most branches. */
    vm.state[0] = 4;
    vm.state[1] = 3;
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_TIMER0) == PLUGIN_VM_OK);
    assert(vm.state[0] == 4 && vm.state[1] == 4);

    vm.state[0] = 4;
    vm.state[1] = 4;
    vm.state[2] = 7;
    vm.state[4] = 7;
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(vm.state[2] == 8 && vm.state[4] == 8 && context.saved_best == 8);
    assert(vm.state[6] == 1 && context.kv_saves == 1U);

    /* A long session must never reach the host's fatal 129th KV write. */
    for (int32_t score = 9; score <= 140; ++score) {
        vm.state[0] = vm.state[1];
        vm.state[3] = 3;
        assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
        assert(vm.state[2] == score && vm.state[4] == score);
    }
    assert(vm.state[6] == 120 && context.kv_saves == 120U);
    assert(context.saved_best == 127);

    vm.state[0] = 1;
    vm.state[1] = 0;
    vm.state[3] = 1;
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(vm.state[3] == 0);
    int32_t stopped_target = vm.state[1];
    unsigned stopped_commits = context.commits;
    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_TIMER0) == PLUGIN_VM_OK);
    assert(vm.state[1] == stopped_target && context.commits == stopped_commits);

    assert(plugin_vm_dispatch(&vm, PLUGIN_EVENT_OK) == PLUGIN_VM_OK);
    assert(vm.state[0] == 2 && vm.state[1] == 0 && vm.state[2] == 0);
    assert(vm.state[3] == 3 && vm.state[4] == 140 && vm.state[5] == 0);
    assert(vm.state[6] == 120);
    assert(context.max_objects == 20U);

    free(content);
    puts("Meteor Tap plugin VM test passed");
    return 0;
}
