#include "plugin_manager_model.h"

void plugin_manager_model_reset(plugin_manager_model_t *model, size_t count)
{
    if (!model) return;
    *model = (plugin_manager_model_t) {
        .count = count,
    };
}

void plugin_manager_model_set_count(plugin_manager_model_t *model, size_t count)
{
    if (!model) return;
    model->count = count;
    if (count == 0U) model->selected = 0U;
    else if (model->selected >= count) model->selected = count - 1U;
}

void plugin_manager_model_move(plugin_manager_model_t *model, int direction)
{
    if (!model || model->confirmation_open || model->count == 0U || direction == 0) return;
    if (direction < 0) {
        model->selected = (model->selected + model->count - 1U) % model->count;
    } else {
        model->selected = (model->selected + 1U) % model->count;
    }
}

bool plugin_manager_model_begin_remove(plugin_manager_model_t *model)
{
    if (!model || model->count == 0U || model->selected >= model->count) return false;
    model->confirmation_open = true;
    model->allow_remove = false;
    return true;
}

void plugin_manager_model_toggle_remove(plugin_manager_model_t *model)
{
    if (!model || !model->confirmation_open) return;
    model->allow_remove = !model->allow_remove;
}

void plugin_manager_model_cancel_remove(plugin_manager_model_t *model)
{
    if (!model) return;
    model->confirmation_open = false;
    model->allow_remove = false;
}

plugin_manager_confirm_result_t plugin_manager_model_confirm(
    plugin_manager_model_t *model)
{
    if (!model || !model->confirmation_open) return PLUGIN_MANAGER_CONFIRM_NONE;
    plugin_manager_confirm_result_t result = model->allow_remove
        ? PLUGIN_MANAGER_CONFIRM_REMOVE
        : PLUGIN_MANAGER_CONFIRM_CANCEL;
    plugin_manager_model_cancel_remove(model);
    return result;
}

size_t plugin_manager_model_window(const plugin_manager_model_t *model,
                                   size_t visible_rows)
{
    if (!model || visible_rows == 0U || model->count <= visible_rows ||
        model->selected < visible_rows) {
        return 0U;
    }
    size_t window = model->selected - visible_rows + 1U;
    size_t maximum = model->count - visible_rows;
    return window < maximum ? window : maximum;
}
