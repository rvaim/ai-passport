#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PLUGIN_MANAGER_CONFIRM_NONE,
    PLUGIN_MANAGER_CONFIRM_CANCEL,
    PLUGIN_MANAGER_CONFIRM_REMOVE,
} plugin_manager_confirm_result_t;

typedef struct {
    size_t count;
    size_t selected;
    bool confirmation_open;
    bool allow_remove;
} plugin_manager_model_t;

void plugin_manager_model_reset(plugin_manager_model_t *model, size_t count);
void plugin_manager_model_set_count(plugin_manager_model_t *model, size_t count);
void plugin_manager_model_move(plugin_manager_model_t *model, int direction);
bool plugin_manager_model_begin_remove(plugin_manager_model_t *model);
void plugin_manager_model_toggle_remove(plugin_manager_model_t *model);
void plugin_manager_model_cancel_remove(plugin_manager_model_t *model);
plugin_manager_confirm_result_t plugin_manager_model_confirm(
    plugin_manager_model_t *model);
size_t plugin_manager_model_window(const plugin_manager_model_t *model,
                                   size_t visible_rows);
