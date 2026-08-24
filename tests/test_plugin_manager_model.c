#include "plugin_manager_model.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    plugin_manager_model_t model;
    plugin_manager_model_reset(&model, 0U);
    assert(!plugin_manager_model_begin_remove(&model));
    plugin_manager_model_move(&model, 1);
    assert(model.selected == 0U);

    plugin_manager_model_set_count(&model, 6U);
    plugin_manager_model_move(&model, -1);
    assert(model.selected == 5U);
    assert(plugin_manager_model_window(&model, 4U) == 2U);
    plugin_manager_model_move(&model, 1);
    assert(model.selected == 0U);

    assert(plugin_manager_model_begin_remove(&model));
    assert(model.confirmation_open && !model.allow_remove);
    plugin_manager_model_move(&model, 1);
    assert(model.selected == 0U);
    assert(plugin_manager_model_confirm(&model) == PLUGIN_MANAGER_CONFIRM_CANCEL);
    assert(!model.confirmation_open);

    assert(plugin_manager_model_begin_remove(&model));
    plugin_manager_model_toggle_remove(&model);
    assert(model.allow_remove);
    assert(plugin_manager_model_confirm(&model) == PLUGIN_MANAGER_CONFIRM_REMOVE);
    assert(!model.confirmation_open && !model.allow_remove);
    assert(plugin_manager_model_confirm(&model) == PLUGIN_MANAGER_CONFIRM_NONE);

    plugin_manager_model_move(&model, -1);
    plugin_manager_model_set_count(&model, 2U);
    assert(model.selected == 1U);
    plugin_manager_model_set_count(&model, 0U);
    assert(model.selected == 0U);

    puts("plugin manager model tests passed");
    return 0;
}
