#pragma once

#include "lua.h"
#include "passport_runtime.h"
#include <stdint.h>

#define PASSPORT_RUNTIME_STORAGE_PENDING_MAX 2U

typedef struct {
    uint32_t request_id;
    int callback_ref;
} passport_runtime_storage_pending_t;

typedef struct {
    lua_State *L;
    char app_id[PASSPORT_MANIFEST_ID_MAX];
    passport_runtime_storage_pending_t pending[PASSPORT_RUNTIME_STORAGE_PENDING_MAX];
} passport_runtime_storage_state_t;

void passport_runtime_storage_start(passport_runtime_storage_state_t *state,
                                    lua_State *L, const char *app_id);
void passport_runtime_storage_register(lua_State *L,
                                       passport_runtime_storage_state_t *state);
void passport_runtime_storage_stop(passport_runtime_storage_state_t *state);
void passport_runtime_storage_handle_completion(
    passport_runtime_storage_state_t *state,
    passport_app_storage_completion_t *completion);

void passport_runtime_storage_set_dispatcher(
    passport_runtime_storage_dispatcher_t dispatcher, void *user);
