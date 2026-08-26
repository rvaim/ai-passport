#include "passport_runtime.h"

#include "passport_identity.h"
#include "passport_link.h"
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

static const char *TAG = "passport_lua";

typedef struct {
    size_t used;
    size_t limit;
} lua_heap_t;

typedef struct {
    size_t size;
} lua_block_t;

typedef struct {
    lua_State *L;
    lua_heap_t heap;
    passport_page_t *page;
    int key_cb_ref;
    int message_cb_ref;
    char app_id[PASSPORT_MANIFEST_ID_MAX];
    uint32_t service_id;
} runtime_t;

static runtime_t s_rt;

static void *limited_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)osize;
    lua_heap_t *heap = (lua_heap_t *)ud;
    if (!ptr) {
        if (nsize == 0 || heap->used + nsize > heap->limit) return NULL;
        lua_block_t *block = malloc(sizeof(*block) + nsize);
        if (!block) return NULL;
        block->size = nsize;
        heap->used += nsize;
        return block + 1;
    }

    lua_block_t *old = ((lua_block_t *)ptr) - 1;
    if (nsize == 0) {
        heap->used -= old->size;
        free(old);
        return NULL;
    }
    if (heap->used - old->size + nsize > heap->limit) return NULL;
    size_t old_size = old->size;
    lua_block_t *next = realloc(old, sizeof(*next) + nsize);
    if (!next) return NULL;
    next->size = nsize;
    heap->used = heap->used - old_size + nsize;
    return next + 1;
}

static int l_ui_page(lua_State *L)
{
    const char *title = luaL_checkstring(L, 1);
    bool status = lua_gettop(L) < 2 || lua_toboolean(L, 2);
    bool keybar = lua_gettop(L) < 3 || lua_toboolean(L, 3);
    if (s_rt.page) passport_ui_page_destroy(s_rt.page);
    s_rt.page = passport_ui_page_create(title, status, keybar);
    if (!s_rt.page) return luaL_error(L, "页面创建失败");
    passport_ui_page_show(s_rt.page);
    return 0;
}

static int l_ui_text(lua_State *L)
{
    if (!s_rt.page) return luaL_error(L, "请先创建页面");
    const char *text = luaL_checkstring(L, 1);
    lv_obj_t *label = passport_ui_label_create(s_rt.page, text);
    if (!label) return luaL_error(L, "文本创建失败");
    lua_pushlightuserdata(L, label);
    return 1;
}

static int l_ui_set_text(lua_State *L)
{
    lv_obj_t *label = (lv_obj_t *)lua_touserdata(L, 1);
    const char *text = luaL_checkstring(L, 2);
    if (!label) return luaL_error(L, "无效文本对象");
    passport_ui_label_set_text(label, text);
    return 0;
}

static int l_ui_actions(lua_State *L)
{
    if (!s_rt.page) return luaL_error(L, "请先创建页面");
    const char *ok_action = luaL_optstring(L, 1, "");
    const char *long_ok_action = luaL_optstring(L, 2, "");
    passport_ui_page_set_actions(s_rt.page, ok_action, long_ok_action);
    return 0;
}

static int l_ui_status_bar(lua_State *L)
{
    if (!s_rt.page) return luaL_error(L, "请先创建页面");
    passport_ui_page_set_status_bar(s_rt.page, lua_toboolean(L, 1));
    return 0;
}

static int l_ui_key_bar(lua_State *L)
{
    if (!s_rt.page) return luaL_error(L, "请先创建页面");
    passport_ui_page_set_key_bar(s_rt.page, lua_toboolean(L, 1));
    return 0;
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
    esp_err_t err = passport_link_send(target_id, s_rt.app_id, PASSPORT_LINK_TYPE_MESSAGE, message, len);
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

static void register_passport_api(lua_State *L)
{
    static const luaL_Reg ui[] = {
        {"page", l_ui_page}, {"text", l_ui_text}, {"set_text", l_ui_set_text},
        {"actions", l_ui_actions}, {"status_bar", l_ui_status_bar}, {"key_bar", l_ui_key_bar}, {NULL, NULL},
    };
    static const luaL_Reg app[] = {
        {"on_key", l_app_on_key}, {"on_message", l_app_on_message}, {NULL, NULL},
    };
    static const luaL_Reg device[] = {{"code", l_device_code}, {NULL, NULL}};
    static const luaL_Reg link[] = {{"send", l_link_send}, {NULL, NULL}};

    lua_newtable(L);
    set_functions(L, ui); lua_setfield(L, -2, "ui");
    set_functions(L, app); lua_setfield(L, -2, "app");
    set_functions(L, device); lua_setfield(L, -2, "device");
    set_functions(L, link); lua_setfield(L, -2, "link");
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

static void log_lua_error(lua_State *L, const char *stage)
{
    const char *msg = lua_tostring(L, -1);
    ESP_LOGE(TAG, "%s: %s", stage, msg ? msg : "未知 Lua 错误");
    lua_pop(L, 1);
}

esp_err_t passport_runtime_start(const passport_app_info_t *app)
{
    if (!app || s_rt.L) return ESP_ERR_INVALID_STATE;
    memset(&s_rt, 0, sizeof(s_rt));
    s_rt.heap.limit = LUA_HEAP_LIMIT;
    s_rt.key_cb_ref = LUA_NOREF;
    s_rt.message_cb_ref = LUA_NOREF;
    memcpy(s_rt.app_id, app->manifest.id, strlen(app->manifest.id) + 1);
    s_rt.service_id = passport_link_service_id(s_rt.app_id);
    s_rt.L = lua_newstate(limited_alloc, &s_rt.heap, esp_random());
    if (!s_rt.L) return ESP_ERR_NO_MEM;
    open_safe_libraries(s_rt.L);
    register_passport_api(s_rt.L);

    char script[256];
    if (snprintf(script, sizeof(script), "%s/%s", app->root, app->manifest.entry) >= (int)sizeof(script)) {
        passport_runtime_stop();
        return ESP_ERR_INVALID_SIZE;
    }
    if (luaL_loadfile(s_rt.L, script) != LUA_OK || lua_pcall(s_rt.L, 0, 0, 0) != LUA_OK) {
        log_lua_error(s_rt.L, "插件启动失败");
        passport_runtime_stop();
        return ESP_FAIL;
    }
    if (!s_rt.page) {
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
    } else lua_pop(s_rt.L, 1);
    if (s_rt.page) {
        passport_ui_page_destroy(s_rt.page);
        s_rt.page = NULL;
    }
    lua_close(s_rt.L);
    memset(&s_rt, 0, sizeof(s_rt));
}

bool passport_runtime_running(void) { return s_rt.L != NULL; }

static const char *key_name(bsp_btn_t btn)
{
    switch (btn) {
    case BSP_BTN_UP: return "up";
    case BSP_BTN_DOWN: return "down";
    case BSP_BTN_OK: return "ok";
    default: return "unknown";
    }
}

static const char *event_name(bsp_btn_ev_t ev)
{
    switch (ev) {
    case BSP_BTN_PRESS: return "press";
    case BSP_BTN_CLICK: return "click";
    case BSP_BTN_DOUBLE: return "double";
    case BSP_BTN_LONG: return "long";
    default: return "unknown";
    }
}

void passport_runtime_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_rt.L || s_rt.key_cb_ref == LUA_NOREF) return;
    lua_rawgeti(s_rt.L, LUA_REGISTRYINDEX, s_rt.key_cb_ref);
    lua_pushstring(s_rt.L, key_name(btn));
    lua_pushstring(s_rt.L, event_name(ev));
    if (lua_pcall(s_rt.L, 2, 0, 0) != LUA_OK) log_lua_error(s_rt.L, "按键回调失败");
}

void passport_runtime_handle_link(const passport_link_frame_t *frame)
{
    if (!s_rt.L || !frame || s_rt.message_cb_ref == LUA_NOREF || frame->service != s_rt.service_id) return;
    char source_code[PASSPORT_DEVICE_CODE_MAX];
    passport_identity_format(frame->source_id, source_code);
    lua_rawgeti(s_rt.L, LUA_REGISTRYINDEX, s_rt.message_cb_ref);
    lua_pushlstring(s_rt.L, (const char *)frame->payload, frame->payload_len);
    lua_pushstring(s_rt.L, source_code);
    if (lua_pcall(s_rt.L, 2, 0, 0) != LUA_OK) log_lua_error(s_rt.L, "消息回调失败");
}
