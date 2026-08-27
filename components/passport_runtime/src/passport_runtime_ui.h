#pragma once

#include "lua.h"
#include "passport_ui.h"
#include <stddef.h>
#include <stdint.h>

typedef struct passport_runtime_ui_resource passport_runtime_ui_resource_t;

typedef struct {
    passport_page_t *page;
    passport_runtime_ui_resource_t *resources;
    const char *app_root;
    size_t buffer_used;
    uint32_t generation;
    uint8_t object_count;
} passport_runtime_ui_state_t;

/** Push the passport.ui table and bind it to the singleton runtime UI state. */
void passport_runtime_ui_register(lua_State *L,
                                  passport_runtime_ui_state_t *state);

/** Begin a newly rendered route. The previous page must already be released. */
void passport_runtime_ui_set_page(passport_runtime_ui_state_t *state,
                                  passport_page_t *page,
                                  uint32_t generation);

/** Release image, line, and canvas buffers after the LVGL page is deleted. */
void passport_runtime_ui_release_page(passport_runtime_ui_state_t *state);
