#include "passport_runtime_storage.h"

#include "esp_log.h"
#include "lauxlib.h"
#include <string.h>

static const char *TAG = "passport_lua_storage";
static passport_runtime_storage_state_t *s_storage;
static passport_runtime_storage_dispatcher_t s_dispatcher;
static void *s_dispatcher_user;
static uint32_t s_next_request_id;

typedef struct {
    const char *name;
    lua_Integer value;
} lua_enum_value_t;

static uint32_t next_request_id(void)
{
    ++s_next_request_id;
    if (s_next_request_id == 0U) ++s_next_request_id;
    return s_next_request_id;
}

static passport_runtime_storage_pending_t *reserve_pending(lua_State *L,
                                                           int callback_argument,
                                                           uint32_t request_id)
{
    luaL_checktype(L, callback_argument, LUA_TFUNCTION);
    if (!s_storage || s_storage->L != L) return NULL;
    for (size_t i = 0; i < PASSPORT_RUNTIME_STORAGE_PENDING_MAX; ++i) {
        passport_runtime_storage_pending_t *pending = &s_storage->pending[i];
        if (pending->callback_ref != LUA_NOREF) continue;
        lua_pushvalue(L, callback_argument);
        pending->callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        pending->request_id = request_id;
        return pending;
    }
    return NULL;
}

static void release_pending(lua_State *L,
                            passport_runtime_storage_pending_t *pending)
{
    if (!pending) return;
    if (pending->callback_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, pending->callback_ref);
    }
    pending->callback_ref = LUA_NOREF;
    pending->request_id = 0U;
}

static void storage_complete_from_worker(
    passport_app_storage_completion_t *completion, void *user)
{
    (void)user;
    if (s_dispatcher) {
        s_dispatcher(completion, s_dispatcher_user);
    } else {
        passport_app_storage_completion_free(completion);
    }
}

static int push_submit_result(lua_State *L, uint32_t request_id,
                              passport_app_storage_error_t error,
                              passport_runtime_storage_pending_t *pending)
{
    if (error == PASSPORT_APP_STORAGE_OK) {
        lua_pushinteger(L, request_id);
        lua_pushinteger(L, PASSPORT_APP_STORAGE_OK);
    } else {
        release_pending(L, pending);
        lua_pushnil(L);
        lua_pushinteger(L, error);
    }
    return 2;
}

static int l_storage_read(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    uint32_t request_id = next_request_id();
    passport_runtime_storage_pending_t *pending = reserve_pending(L, 2, request_id);
    if (!pending) return push_submit_result(
        L, request_id, PASSPORT_APP_STORAGE_BUSY, NULL);
    passport_app_storage_error_t error = passport_app_storage_read_async(
        s_storage->app_id, path, request_id, storage_complete_from_worker, NULL);
    return push_submit_result(L, request_id, error, pending);
}

static int l_storage_write(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    size_t size = 0U;
    const char *data = luaL_checklstring(L, 2, &size);
    uint32_t request_id = next_request_id();
    passport_runtime_storage_pending_t *pending = reserve_pending(L, 3, request_id);
    if (!pending) return push_submit_result(
        L, request_id, PASSPORT_APP_STORAGE_BUSY, NULL);
    passport_app_storage_error_t error = passport_app_storage_write_async(
        s_storage->app_id, path, data, size, request_id,
        storage_complete_from_worker, NULL);
    return push_submit_result(L, request_id, error, pending);
}

static int l_storage_remove(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    uint32_t request_id = next_request_id();
    passport_runtime_storage_pending_t *pending = reserve_pending(L, 2, request_id);
    if (!pending) return push_submit_result(
        L, request_id, PASSPORT_APP_STORAGE_BUSY, NULL);
    passport_app_storage_error_t error = passport_app_storage_remove_async(
        s_storage->app_id, path, request_id, storage_complete_from_worker, NULL);
    return push_submit_result(L, request_id, error, pending);
}

static int l_storage_list(lua_State *L)
{
    const char *path = "";
    int callback_argument = 1;
    if (!lua_isfunction(L, 1)) {
        path = luaL_checkstring(L, 1);
        callback_argument = 2;
    }
    uint32_t request_id = next_request_id();
    passport_runtime_storage_pending_t *pending = reserve_pending(
        L, callback_argument, request_id);
    if (!pending) return push_submit_result(
        L, request_id, PASSPORT_APP_STORAGE_BUSY, NULL);
    passport_app_storage_error_t error = passport_app_storage_list_async(
        s_storage->app_id, path, request_id, storage_complete_from_worker, NULL);
    return push_submit_result(L, request_id, error, pending);
}

static int l_storage_usage(lua_State *L)
{
    uint32_t request_id = next_request_id();
    passport_runtime_storage_pending_t *pending = reserve_pending(L, 1, request_id);
    if (!pending) return push_submit_result(
        L, request_id, PASSPORT_APP_STORAGE_BUSY, NULL);
    passport_app_storage_error_t error = passport_app_storage_usage_async(
        s_storage->app_id, request_id, storage_complete_from_worker, NULL);
    return push_submit_result(L, request_id, error, pending);
}

static void set_error_enum(lua_State *L)
{
    static const lua_enum_value_t values[] = {
        {"OK", PASSPORT_APP_STORAGE_OK},
        {"NOT_FOUND", PASSPORT_APP_STORAGE_NOT_FOUND},
        {"INVALID_PATH", PASSPORT_APP_STORAGE_INVALID_PATH},
        {"TOO_LARGE", PASSPORT_APP_STORAGE_TOO_LARGE},
        {"QUOTA_EXCEEDED", PASSPORT_APP_STORAGE_QUOTA_EXCEEDED},
        {"NO_SPACE", PASSPORT_APP_STORAGE_NO_SPACE},
        {"BUSY", PASSPORT_APP_STORAGE_BUSY},
        {"IO_ERROR", PASSPORT_APP_STORAGE_IO_ERROR},
        {"CANCELED", PASSPORT_APP_STORAGE_CANCELED},
        {"NO_MEMORY", PASSPORT_APP_STORAGE_NO_MEMORY},
        {NULL, 0},
    };
    lua_newtable(L);
    for (size_t i = 0; values[i].name; ++i) {
        lua_pushinteger(L, values[i].value);
        lua_setfield(L, -2, values[i].name);
    }
    lua_setfield(L, -2, "Error");
}

void passport_runtime_storage_start(passport_runtime_storage_state_t *state,
                                    lua_State *L, const char *app_id)
{
    memset(state, 0, sizeof(*state));
    state->L = L;
    memcpy(state->app_id, app_id, strlen(app_id) + 1U);
    for (size_t i = 0; i < PASSPORT_RUNTIME_STORAGE_PENDING_MAX; ++i) {
        state->pending[i].callback_ref = LUA_NOREF;
    }
    s_storage = state;
}

void passport_runtime_storage_register(lua_State *L,
                                       passport_runtime_storage_state_t *state)
{
    static const luaL_Reg functions[] = {
        {"read", l_storage_read},
        {"write", l_storage_write},
        {"remove", l_storage_remove},
        {"list", l_storage_list},
        {"usage", l_storage_usage},
        {NULL, NULL},
    };
    s_storage = state;
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    set_error_enum(L);
}

void passport_runtime_storage_stop(passport_runtime_storage_state_t *state)
{
    if (!state || !state->L) return;
    for (size_t i = 0; i < PASSPORT_RUNTIME_STORAGE_PENDING_MAX; ++i) {
        release_pending(state->L, &state->pending[i]);
    }
    state->L = NULL;
    state->app_id[0] = '\0';
    if (s_storage == state) s_storage = NULL;
}

static passport_runtime_storage_pending_t *find_pending(
    passport_runtime_storage_state_t *state, uint32_t request_id)
{
    for (size_t i = 0; i < PASSPORT_RUNTIME_STORAGE_PENDING_MAX; ++i) {
        if (state->pending[i].request_id == request_id &&
            state->pending[i].callback_ref != LUA_NOREF) {
            return &state->pending[i];
        }
    }
    return NULL;
}

static int push_list_result(lua_State *L,
                            const passport_app_storage_completion_t *completion)
{
    if (completion->error != PASSPORT_APP_STORAGE_OK) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, (int)completion->entry_count, 0);
    for (size_t i = 0; i < completion->entry_count; ++i) {
        const passport_app_storage_entry_t *entry = &completion->entries[i];
        lua_createtable(L, 0, 3);
        lua_pushstring(L, entry->name); lua_setfield(L, -2, "name");
        lua_pushinteger(L, entry->size); lua_setfield(L, -2, "size");
        lua_pushboolean(L, entry->is_directory); lua_setfield(L, -2, "is_directory");
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}

void passport_runtime_storage_handle_completion(
    passport_runtime_storage_state_t *state,
    passport_app_storage_completion_t *completion)
{
    if (!completion) return;
    if (!state || !state->L || strcmp(state->app_id, completion->app_id) != 0) {
        passport_app_storage_completion_free(completion);
        return;
    }
    passport_runtime_storage_pending_t *pending = find_pending(
        state, completion->request_id);
    if (!pending) {
        passport_app_storage_completion_free(completion);
        return;
    }
    lua_State *L = state->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, pending->callback_ref);
    release_pending(L, pending);
    lua_pushinteger(L, completion->error);
    int arguments = 1;
    if (completion->operation == PASSPORT_APP_STORAGE_READ) {
        if (completion->error == PASSPORT_APP_STORAGE_OK) {
            lua_pushlstring(L, (const char *)(completion->data ? completion->data :
                            (const uint8_t *)""), completion->data_size);
        } else {
            lua_pushnil(L);
        }
        ++arguments;
    } else if (completion->operation == PASSPORT_APP_STORAGE_LIST) {
        arguments += push_list_result(L, completion);
    } else if (completion->operation == PASSPORT_APP_STORAGE_USAGE) {
        if (completion->error == PASSPORT_APP_STORAGE_OK) {
            lua_pushinteger(L, completion->used_bytes);
            lua_pushinteger(L, completion->quota_bytes);
            lua_pushinteger(L, completion->file_count);
        } else {
            lua_pushnil(L); lua_pushnil(L); lua_pushnil(L);
        }
        arguments += 3;
    }
    if (lua_pcall(L, arguments, 0, 0) != LUA_OK) {
        const char *message = lua_tostring(L, -1);
        ESP_LOGE(TAG, "存储回调失败: %s", message ? message : "未知 Lua 错误");
        lua_pop(L, 1);
    }
    passport_app_storage_completion_free(completion);
}

void passport_runtime_storage_set_dispatcher(
    passport_runtime_storage_dispatcher_t dispatcher, void *user)
{
    s_dispatcher = dispatcher;
    s_dispatcher_user = user;
}
