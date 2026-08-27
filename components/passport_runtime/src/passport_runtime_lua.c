#include "passport_runtime.h"
#include "passport_runtime_json.h"
#include "passport_runtime_storage.h"
#include "passport_runtime_ui.h"

#include "passport_identity.h"
#include "passport_input.h"
#include "passport_link.h"
#include "passport_navigation.h"
#include "passport_ui.h"
#include "esp_log.h"
#include "esp_random.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LUA_HEAP_LIMIT (80U * 1024U)
#define PAP_ROUTE_TITLE_MAX 48U

_Static_assert(sizeof(lua_Integer) == 4U && sizeof(lua_Number) == 4U,
               "Passport runtime must match the Lua core's 32-bit ABI");

static const char *TAG = "passport_lua";

typedef struct {
    size_t used;
    size_t limit;
    size_t peak;
    size_t rejected_size;
} lua_heap_t;

typedef struct {
    size_t size;
} lua_block_t;

typedef struct {
    lua_State *L;
    lua_heap_t heap;
    passport_page_t *page;
    passport_navigation_t navigation;
    char route_titles[PASSPORT_NAVIGATION_MAX_DEPTH][PAP_ROUTE_TITLE_MAX];
    int key_cb_ref;
    int message_cb_ref;
    passport_runtime_ui_state_t ui;
    passport_runtime_storage_state_t storage;
    uint32_t page_generation;
    bool rendering;
    char app_id[PASSPORT_MANIFEST_ID_MAX];
    char app_root[160];
    uint32_t service_id;
} runtime_t;

static runtime_t s_rt;

static void *limited_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)osize;
    lua_heap_t *heap = (lua_heap_t *)ud;
    if (!ptr) {
        if (nsize == 0U) return NULL;
        if (nsize > heap->limit - heap->used) {
            heap->rejected_size = nsize;
            return NULL;
        }
        lua_block_t *block = malloc(sizeof(*block) + nsize);
        if (!block) {
            heap->rejected_size = nsize;
            return NULL;
        }
        block->size = nsize;
        heap->used += nsize;
        if (heap->used > heap->peak) heap->peak = heap->used;
        return block + 1;
    }

    lua_block_t *old = ((lua_block_t *)ptr) - 1;
    if (nsize == 0U) {
        heap->used -= old->size;
        free(old);
        return NULL;
    }
    size_t retained = heap->used - old->size;
    if (nsize > heap->limit - retained) {
        heap->rejected_size = nsize;
        return NULL;
    }
    size_t old_size = old->size;
    lua_block_t *next = realloc(old, sizeof(*next) + nsize);
    if (!next) {
        heap->rejected_size = nsize;
        return NULL;
    }
    next->size = nsize;
    heap->used = heap->used - old_size + nsize;
    if (heap->used > heap->peak) heap->peak = heap->used;
    return next + 1;
}

static void log_lua_error(lua_State *L, const char *stage)
{
    const char *msg = lua_tostring(L, -1);
    ESP_LOGE(TAG, "%s: %s", stage, msg ? msg : "未知 Lua 错误");
    lua_pop(L, 1);
}

static void clear_page_key_callback(void)
{
    if (s_rt.L && s_rt.key_cb_ref != LUA_NOREF) {
        luaL_unref(s_rt.L, LUA_REGISTRYINDEX, s_rt.key_cb_ref);
    }
    s_rt.key_cb_ref = LUA_NOREF;
}

static void destroy_page(void)
{
    if (s_rt.page) passport_ui_page_destroy(s_rt.page);
    s_rt.page = NULL;
    passport_runtime_ui_release_page(&s_rt.ui);
    ++s_rt.page_generation;
}

static bool render_current_route(void)
{
    const passport_navigation_frame_t *frame = passport_navigation_current(
        &s_rt.navigation);
    if (!s_rt.L || !frame || frame->route == (uint32_t)LUA_NOREF) return false;

    clear_page_key_callback();
    destroy_page();
    size_t index = passport_navigation_depth(&s_rt.navigation) - 1U;
    s_rt.page = passport_ui_page_create(s_rt.route_titles[index], true, true);
    if (!s_rt.page) return false;
    passport_runtime_ui_set_page(&s_rt.ui, s_rt.page, s_rt.page_generation);
    passport_ui_page_set_can_go_back(
        s_rt.page, passport_navigation_can_pop(&s_rt.navigation));

    lua_rawgeti(s_rt.L, LUA_REGISTRYINDEX, (int)frame->route);
    s_rt.rendering = true;
    int status = lua_pcall(s_rt.L, 0, 0, 0);
    s_rt.rendering = false;
    if (status != LUA_OK) {
        log_lua_error(s_rt.L, "页面显示失败");
        destroy_page();
        return false;
    }
    passport_ui_page_show(s_rt.page);
    return true;
}

static void unref_routes(void)
{
    if (!s_rt.L) return;
    for (size_t i = 0; i < passport_navigation_depth(&s_rt.navigation); ++i) {
        int ref = (int)s_rt.navigation.frames[i].route;
        if (ref != LUA_NOREF) luaL_unref(s_rt.L, LUA_REGISTRYINDEX, ref);
    }
    memset(&s_rt.navigation, 0, sizeof(s_rt.navigation));
    memset(s_rt.route_titles, 0, sizeof(s_rt.route_titles));
}

static const char *check_route_title(lua_State *L, int argument)
{
    size_t length = 0;
    const char *title = luaL_checklstring(L, argument, &length);
    if (length == 0U || length >= PAP_ROUTE_TITLE_MAX) {
        luaL_argerror(L, argument, "标题长度必须为 1..47 字节");
    }
    return title;
}

static int reference_function(lua_State *L, int argument)
{
    luaL_checktype(L, argument, LUA_TFUNCTION);
    lua_pushvalue(L, argument);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

static void copy_route_title(size_t index, const char *title)
{
    memcpy(s_rt.route_titles[index], title, strlen(title) + 1U);
}

static int l_navigation_set_root(lua_State *L)
{
    if (s_rt.rendering) return luaL_error(L, "显示页面时不能修改导航栈");
    const char *title = check_route_title(L, 1);
    int ref = reference_function(L, 2);
    unref_routes();
    passport_navigation_reset(&s_rt.navigation, (uint32_t)ref, 0);
    copy_route_title(0, title);
    if (!render_current_route()) return luaL_error(L, "根页面显示失败");
    return 0;
}

static int l_navigation_push(lua_State *L)
{
    if (s_rt.rendering) return luaL_error(L, "显示页面时不能修改导航栈");
    if (passport_navigation_depth(&s_rt.navigation) == 0U) {
        return luaL_error(L, "请先设置根页面");
    }
    if (passport_navigation_depth(&s_rt.navigation) >= PASSPORT_NAVIGATION_MAX_DEPTH) {
        return luaL_error(L, "导航栈已满");
    }
    const char *title = check_route_title(L, 1);
    int ref = reference_function(L, 2);
    size_t index = passport_navigation_depth(&s_rt.navigation);
    passport_navigation_push(&s_rt.navigation, (uint32_t)ref, 0);
    copy_route_title(index, title);
    if (!render_current_route()) {
        passport_navigation_pop(&s_rt.navigation);
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        s_rt.route_titles[index][0] = '\0';
        render_current_route();
        return luaL_error(L, "新页面显示失败");
    }
    return 0;
}

static int l_navigation_replace(lua_State *L)
{
    if (s_rt.rendering) return luaL_error(L, "显示页面时不能修改导航栈");
    const passport_navigation_frame_t *current = passport_navigation_current(
        &s_rt.navigation);
    if (!current) return luaL_error(L, "请先设置根页面");
    const char *title = check_route_title(L, 1);
    int new_ref = reference_function(L, 2);
    int old_ref = (int)current->route;
    size_t index = passport_navigation_depth(&s_rt.navigation) - 1U;
    char old_title[PAP_ROUTE_TITLE_MAX];
    memcpy(old_title, s_rt.route_titles[index], sizeof(old_title));
    passport_navigation_replace(&s_rt.navigation, (uint32_t)new_ref, 0);
    copy_route_title(index, title);
    if (!render_current_route()) {
        passport_navigation_replace(&s_rt.navigation, (uint32_t)old_ref, 0);
        memcpy(s_rt.route_titles[index], old_title, sizeof(old_title));
        luaL_unref(L, LUA_REGISTRYINDEX, new_ref);
        render_current_route();
        return luaL_error(L, "替换页面显示失败");
    }
    luaL_unref(L, LUA_REGISTRYINDEX, old_ref);
    return 0;
}

static int l_navigation_pop(lua_State *L)
{
    if (s_rt.rendering) return luaL_error(L, "显示页面时不能修改导航栈");
    if (!passport_navigation_can_pop(&s_rt.navigation)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    size_t old_index = passport_navigation_depth(&s_rt.navigation) - 1U;
    int old_ref = (int)s_rt.navigation.frames[old_index].route;
    char old_title[PAP_ROUTE_TITLE_MAX];
    memcpy(old_title, s_rt.route_titles[old_index], sizeof(old_title));
    passport_navigation_pop(&s_rt.navigation);
    s_rt.route_titles[old_index][0] = '\0';
    if (!render_current_route()) {
        passport_navigation_push(&s_rt.navigation, (uint32_t)old_ref, 0);
        memcpy(s_rt.route_titles[old_index], old_title, sizeof(old_title));
        render_current_route();
        return luaL_error(L, "返回页面显示失败");
    }
    luaL_unref(L, LUA_REGISTRYINDEX, old_ref);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_navigation_depth(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)passport_navigation_depth(&s_rt.navigation));
    return 1;
}

static int l_navigation_can_pop(lua_State *L)
{
    lua_pushboolean(L, passport_navigation_can_pop(&s_rt.navigation));
    return 1;
}

static int l_device_code(lua_State *L)
{
    lua_pushstring(L, passport_identity_code());
    return 1;
}

static int replace_callback(lua_State *L, int *slot)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (*slot != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, *slot);
    lua_pushvalue(L, 1);
    *slot = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

static int l_app_on_key(lua_State *L) { return replace_callback(L, &s_rt.key_cb_ref); }
static int l_app_on_message(lua_State *L) { return replace_callback(L, &s_rt.message_cb_ref); }

static int l_link_send(lua_State *L)
{
    const char *target_code = luaL_checkstring(L, 1);
    size_t len = 0;
    const char *message = luaL_checklstring(L, 2, &len);
    uint64_t target_id = 0;
    if (passport_identity_parse_code(target_code, &target_id) != ESP_OK) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "设备码无效");
        return 2;
    }
    esp_err_t err = passport_link_send(target_id, s_rt.app_id,
                                       PASSPORT_LINK_TYPE_MESSAGE, message, len);
    lua_pushboolean(L, err == ESP_OK);
    if (err != ESP_OK) lua_pushstring(L, esp_err_to_name(err));
    else lua_pushnil(L);
    return 2;
}

static void set_functions(lua_State *L, const luaL_Reg *functions)
{
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
}

typedef struct {
    const char *name;
    lua_Integer value;
} lua_enum_value_t;

static void set_enum(lua_State *L, const char *name,
                     const lua_enum_value_t *values)
{
    lua_newtable(L);
    for (size_t i = 0; values[i].name; ++i) {
        lua_pushinteger(L, values[i].value);
        lua_setfield(L, -2, values[i].name);
    }
    lua_setfield(L, -2, name);
}

static void register_passport_api(lua_State *L)
{
    static const luaL_Reg navigation[] = {
        {"set_root", l_navigation_set_root}, {"push", l_navigation_push},
        {"replace", l_navigation_replace}, {"pop", l_navigation_pop},
        {"depth", l_navigation_depth}, {"can_pop", l_navigation_can_pop},
        {NULL, NULL},
    };
    static const luaL_Reg app[] = {
        {"on_key", l_app_on_key}, {"on_message", l_app_on_message}, {NULL, NULL},
    };
    static const luaL_Reg device[] = {{"code", l_device_code}, {NULL, NULL}};
    static const luaL_Reg link[] = {{"send", l_link_send}, {NULL, NULL}};
    static const lua_enum_value_t key_values[] = {
        {"UP", PASSPORT_INPUT_KEY_UP}, {"DOWN", PASSPORT_INPUT_KEY_DOWN},
        {"OK", PASSPORT_INPUT_KEY_OK}, {NULL, 0},
    };
    static const lua_enum_value_t event_values[] = {
        {"PRESS", PASSPORT_INPUT_EVENT_PRESS},
        {"CLICK", PASSPORT_INPUT_EVENT_CLICK},
        {"DOUBLE_CLICK", PASSPORT_INPUT_EVENT_DOUBLE_CLICK},
        {"LONG_PRESS", PASSPORT_INPUT_EVENT_LONG_PRESS}, {NULL, 0},
    };

    lua_newtable(L);
    passport_runtime_ui_register(L, &s_rt.ui);
    lua_setfield(L, -2, "ui");
    set_functions(L, navigation); lua_setfield(L, -2, "navigation");
    set_functions(L, app); lua_setfield(L, -2, "app");
    set_functions(L, device); lua_setfield(L, -2, "device");
    set_functions(L, link); lua_setfield(L, -2, "link");
    passport_runtime_register_json(L); lua_setfield(L, -2, "json");
    passport_runtime_storage_register(L, &s_rt.storage);
    lua_setfield(L, -2, "storage");
    lua_newtable(L);
    set_enum(L, "Key", key_values);
    set_enum(L, "KeyEvent", event_values);
    lua_pushboolean(L, passport_input_chords_supported());
    lua_setfield(L, -2, "supports_chords");
    lua_setfield(L, -2, "input");
    lua_setglobal(L, "passport");
}

static void open_safe_libraries(lua_State *L)
{
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L, 1);
}

static int bootstrap_runtime(lua_State *L)
{
    open_safe_libraries(L);
    register_passport_api(L);
    return 0;
}

static void discard_failed_runtime_start(void)
{
    passport_runtime_storage_stop(&s_rt.storage);
    lua_close(s_rt.L);
    memset(&s_rt, 0, sizeof(s_rt));
}

esp_err_t passport_runtime_start(const passport_app_info_t *app)
{
    if (!app || s_rt.L) return ESP_ERR_INVALID_STATE;
    memset(&s_rt, 0, sizeof(s_rt));
    s_rt.heap.limit = LUA_HEAP_LIMIT;
    s_rt.key_cb_ref = LUA_NOREF;
    s_rt.message_cb_ref = LUA_NOREF;
    memcpy(s_rt.app_id, app->manifest.id, strlen(app->manifest.id) + 1U);
    memcpy(s_rt.app_root, app->root, strlen(app->root) + 1U);
    s_rt.ui.app_root = s_rt.app_root;
    s_rt.service_id = passport_link_service_id(s_rt.app_id);
    s_rt.L = lua_newstate(limited_alloc, &s_rt.heap, esp_random());
    if (!s_rt.L) return ESP_ERR_NO_MEM;
    passport_runtime_storage_start(&s_rt.storage, s_rt.L, s_rt.app_id);
    lua_pushcfunction(s_rt.L, bootstrap_runtime);
    if (lua_pcall(s_rt.L, 0, 0, 0) != LUA_OK) {
        log_lua_error(s_rt.L, "运行时初始化失败");
        ESP_LOGE(TAG, "Lua heap=%u/%u, peak=%u, rejected=%u",
                 (unsigned)s_rt.heap.used, (unsigned)s_rt.heap.limit,
                 (unsigned)s_rt.heap.peak, (unsigned)s_rt.heap.rejected_size);
        esp_err_t err = s_rt.heap.rejected_size ? ESP_ERR_NO_MEM : ESP_FAIL;
        discard_failed_runtime_start();
        return err;
    }

    char script[256];
    if (snprintf(script, sizeof(script), "%s/%s", app->root,
                 app->manifest.entry) >= (int)sizeof(script)) {
        passport_runtime_stop();
        return ESP_ERR_INVALID_SIZE;
    }
    if (luaL_loadfile(s_rt.L, script) != LUA_OK ||
        lua_pcall(s_rt.L, 0, 0, 0) != LUA_OK) {
        log_lua_error(s_rt.L, "插件启动失败");
        passport_runtime_stop();
        return ESP_FAIL;
    }
    if (!s_rt.page || passport_navigation_depth(&s_rt.navigation) == 0U) {
        passport_runtime_stop();
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "插件已启动: %s, Lua heap=%u/%u", s_rt.app_id,
             (unsigned)s_rt.heap.used, (unsigned)s_rt.heap.limit);
    return ESP_OK;
}

void passport_runtime_stop(void)
{
    if (!s_rt.L) return;
    lua_getglobal(s_rt.L, "on_stop");
    if (lua_isfunction(s_rt.L, -1)) {
        if (lua_pcall(s_rt.L, 0, 0, 0) != LUA_OK) log_lua_error(s_rt.L, "on_stop");
    } else {
        lua_pop(s_rt.L, 1);
    }
    passport_runtime_storage_stop(&s_rt.storage);
    clear_page_key_callback();
    destroy_page();
    unref_routes();
    if (s_rt.message_cb_ref != LUA_NOREF) {
        luaL_unref(s_rt.L, LUA_REGISTRYINDEX, s_rt.message_cb_ref);
    }
    lua_close(s_rt.L);
    memset(&s_rt, 0, sizeof(s_rt));
}

bool passport_runtime_running(void)
{
    return s_rt.L != NULL;
}

void passport_runtime_handle_storage_completion(
    passport_app_storage_completion_t *completion)
{
    passport_runtime_storage_handle_completion(&s_rt.storage, completion);
}

bool passport_runtime_navigate_back(void)
{
    if (!s_rt.L || !passport_navigation_can_pop(&s_rt.navigation)) return false;
    size_t old_index = passport_navigation_depth(&s_rt.navigation) - 1U;
    int old_ref = (int)s_rt.navigation.frames[old_index].route;
    passport_navigation_pop(&s_rt.navigation);
    s_rt.route_titles[old_index][0] = '\0';
    if (!render_current_route()) {
        ESP_LOGE(TAG, "返回 PAP 上一页失败，终止运行时");
        passport_runtime_stop();
        return false;
    }
    luaL_unref(s_rt.L, LUA_REGISTRYINDEX, old_ref);
    return true;
}

static passport_input_key_t input_key(bsp_btn_t button)
{
    if (button == BSP_BTN_UP) return PASSPORT_INPUT_KEY_UP;
    if (button == BSP_BTN_DOWN) return PASSPORT_INPUT_KEY_DOWN;
    return PASSPORT_INPUT_KEY_OK;
}

static passport_input_event_t input_event(bsp_btn_ev_t event)
{
    if (event == BSP_BTN_PRESS) return PASSPORT_INPUT_EVENT_PRESS;
    if (event == BSP_BTN_DOUBLE) return PASSPORT_INPUT_EVENT_DOUBLE_CLICK;
    if (event == BSP_BTN_LONG) return PASSPORT_INPUT_EVENT_LONG_PRESS;
    return PASSPORT_INPUT_EVENT_CLICK;
}

void passport_runtime_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_rt.L || s_rt.key_cb_ref == LUA_NOREF) return;
    lua_rawgeti(s_rt.L, LUA_REGISTRYINDEX, s_rt.key_cb_ref);
    lua_pushinteger(s_rt.L, input_key(btn));
    lua_pushinteger(s_rt.L, input_event(ev));
    if (lua_pcall(s_rt.L, 2, 0, 0) != LUA_OK) {
        log_lua_error(s_rt.L, "按键回调失败");
    }
}

void passport_runtime_handle_link(const passport_link_frame_t *frame)
{
    if (!s_rt.L || !frame || s_rt.message_cb_ref == LUA_NOREF ||
        frame->service != s_rt.service_id) {
        return;
    }
    char source_code[PASSPORT_DEVICE_CODE_MAX];
    passport_identity_format(frame->source_id, source_code);
    lua_rawgeti(s_rt.L, LUA_REGISTRYINDEX, s_rt.message_cb_ref);
    lua_pushlstring(s_rt.L, (const char *)frame->payload, frame->payload_len);
    lua_pushstring(s_rt.L, source_code);
    if (lua_pcall(s_rt.L, 2, 0, 0) != LUA_OK) {
        log_lua_error(s_rt.L, "消息回调失败");
    }
}
