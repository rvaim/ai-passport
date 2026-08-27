#include "passport_runtime_json.h"

#include "passport_text.h"
#include "cJSON.h"
#include "lauxlib.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PASSPORT_JSON_MAX_BYTES 4096U
#define PASSPORT_JSON_MAX_DEPTH 12U
#define PASSPORT_JSON_MAX_NODES 128U
static const char s_json_null_marker;
static const char s_json_array_metatable_key;

typedef enum {
    JSON_TABLE_OBJECT,
    JSON_TABLE_ARRAY,
    JSON_TABLE_INVALID,
} json_table_kind_t;

static int push_error(lua_State *L, const char *message)
{
    lua_pushnil(L);
    lua_pushstring(L, message);
    return 2;
}

static bool input_is_safe(const char *json, size_t length, const char **error)
{
    if (length == 0) {
        *error = "JSON 不能为空";
        return false;
    }
    if (length > PASSPORT_JSON_MAX_BYTES) {
        *error = "JSON 超过 4096 字节";
        return false;
    }
    if (memchr(json, '\0', length) != NULL ||
        !passport_text_utf8_is_valid(json, length)) {
        *error = "JSON 必须是有效 UTF-8 且不能包含 NUL";
        return false;
    }
    if (passport_text_json_contains_nul_escape(json, length)) {
        *error = "JSON 字符串不能包含 U+0000";
        return false;
    }

    size_t depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = 0; i < length; ++i) {
        char c = json[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '{' || c == '[') {
            if (++depth > PASSPORT_JSON_MAX_DEPTH) {
                *error = "JSON 嵌套超过 12 层";
                return false;
            }
        } else if ((c == '}' || c == ']') && depth > 0) {
            --depth;
        }
    }
    return true;
}

static bool number_is_safe(double value)
{
    double integer_part;
    if (!isfinite(value)) return false;
    if (modf(value, &integer_part) == 0.0) {
        return integer_part >= (double)LUA_MININTEGER &&
               integer_part <= (double)LUA_MAXINTEGER;
    }
    lua_Number converted = (lua_Number)value;
    return isfinite((double)converted) && (value == 0.0 || converted != 0.0f);
}

static bool cjson_tree_is_safe(const cJSON *item, size_t depth,
                               size_t *nodes, const char **error)
{
    if (!item || depth > PASSPORT_JSON_MAX_DEPTH || ++(*nodes) > PASSPORT_JSON_MAX_NODES) {
        *error = depth > PASSPORT_JSON_MAX_DEPTH ?
                 "JSON 嵌套超过 12 层" : "JSON 值超过 128 个";
        return false;
    }
    if ((cJSON_IsArray(item) || cJSON_IsObject(item)) &&
        depth >= PASSPORT_JSON_MAX_DEPTH) {
        *error = "JSON 嵌套超过 12 层";
        return false;
    }
    if (cJSON_IsNumber(item) && !number_is_safe(item->valuedouble)) {
        *error = "JSON 数字超出安全范围";
        return false;
    }
    if (cJSON_IsString(item) &&
        (!item->valuestring ||
         !passport_text_utf8_is_valid(item->valuestring, strlen(item->valuestring)))) {
        *error = "JSON 字符串不是有效 UTF-8";
        return false;
    }
    if (!cJSON_IsNull(item) && !cJSON_IsBool(item) && !cJSON_IsNumber(item) &&
        !cJSON_IsString(item) && !cJSON_IsArray(item) && !cJSON_IsObject(item)) {
        *error = "JSON 包含不支持的值";
        return false;
    }
    if (cJSON_IsArray(item) || cJSON_IsObject(item)) {
        for (const cJSON *child = item->child; child; child = child->next) {
            if (cJSON_IsObject(item) &&
                (!child->string ||
                 !passport_text_utf8_is_valid(child->string, strlen(child->string)))) {
                *error = "JSON 对象键不是有效 UTF-8";
                return false;
            }
            if (!cjson_tree_is_safe(child, depth + 1, nodes, error)) return false;
        }
    }
    return true;
}

static void mark_array(lua_State *L, int index)
{
    index = lua_absindex(L, index);
    lua_rawgetp(L, LUA_REGISTRYINDEX, &s_json_array_metatable_key);
    lua_setmetatable(L, index);
}

static bool table_is_marked_array(lua_State *L, int index)
{
    index = lua_absindex(L, index);
    if (!lua_getmetatable(L, index)) return false;
    lua_rawgetp(L, LUA_REGISTRYINDEX, &s_json_array_metatable_key);
    bool marked = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return marked;
}

static bool push_cjson_value(lua_State *L, const cJSON *item,
                             size_t depth, const char **error)
{
    if (depth > PASSPORT_JSON_MAX_DEPTH) {
        *error = "JSON 嵌套超过 12 层";
        return false;
    }
    if (cJSON_IsNull(item)) {
        lua_pushlightuserdata(L, (void *)&s_json_null_marker);
        return true;
    }
    if (cJSON_IsBool(item)) {
        lua_pushboolean(L, cJSON_IsTrue(item));
        return true;
    }
    if (cJSON_IsNumber(item)) {
        double integer_part;
        if (modf(item->valuedouble, &integer_part) == 0.0 &&
            integer_part >= (double)LUA_MININTEGER && integer_part <= (double)LUA_MAXINTEGER) {
            lua_pushinteger(L, (lua_Integer)integer_part);
        } else {
            lua_pushnumber(L, (lua_Number)item->valuedouble);
        }
        return true;
    }
    if (cJSON_IsString(item)) {
        lua_pushstring(L, item->valuestring);
        return true;
    }
    if (cJSON_IsArray(item)) {
        int count = cJSON_GetArraySize(item);
        lua_createtable(L, count, 0);
        int table_index = lua_gettop(L);
        mark_array(L, table_index);
        int array_index = 1;
        for (const cJSON *child = item->child; child; child = child->next, ++array_index) {
            if (!push_cjson_value(L, child, depth + 1, error)) {
                lua_pop(L, 1);
                return false;
            }
            lua_rawseti(L, table_index, array_index);
        }
        return true;
    }
    if (cJSON_IsObject(item)) {
        lua_createtable(L, 0, 0);
        int table_index = lua_gettop(L);
        for (const cJSON *child = item->child; child; child = child->next) {
            lua_pushstring(L, child->string);
            lua_rawget(L, table_index);
            bool duplicate = !lua_isnil(L, -1);
            lua_pop(L, 1);
            if (duplicate) {
                lua_pop(L, 1);
                *error = "JSON 对象包含重复键";
                return false;
            }
            lua_pushstring(L, child->string);
            if (!push_cjson_value(L, child, depth + 1, error)) {
                lua_pop(L, 2);
                return false;
            }
            lua_rawset(L, table_index);
        }
        return true;
    }
    *error = "JSON 包含不支持的值";
    return false;
}

static json_table_kind_t classify_table(lua_State *L, int index, const char **error)
{
    index = lua_absindex(L, index);
    bool marked_array = table_is_marked_array(L, index);
    size_t length = lua_rawlen(L, index);
    size_t numeric_keys = 0;
    bool has_string_key = false;

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        int key_type = lua_type(L, -2);
        if (key_type == LUA_TNUMBER && lua_isinteger(L, -2)) {
            lua_Integer key = lua_tointeger(L, -2);
            if (key < 1 || (uint64_t)key > length) {
                lua_pop(L, 2);
                *error = "JSON 数组必须使用连续的 1..n 整数索引";
                return JSON_TABLE_INVALID;
            }
            ++numeric_keys;
        } else if (key_type == LUA_TSTRING) {
            has_string_key = true;
        } else {
            lua_pop(L, 2);
            *error = "JSON 对象键必须是字符串";
            return JSON_TABLE_INVALID;
        }
        lua_pop(L, 1);
    }

    if (marked_array || numeric_keys > 0) {
        if (has_string_key || numeric_keys != length) {
            *error = "JSON 数组不能稀疏或混用字符串键";
            return JSON_TABLE_INVALID;
        }
        return JSON_TABLE_ARRAY;
    }
    return JSON_TABLE_OBJECT;
}

static cJSON *lua_to_cjson(lua_State *L, int index, size_t depth,
                           size_t *nodes, const char **error);

static cJSON *table_to_cjson(lua_State *L, int index, size_t depth,
                             size_t *nodes, const char **error)
{
    index = lua_absindex(L, index);
    json_table_kind_t kind = classify_table(L, index, error);
    if (kind == JSON_TABLE_INVALID) return NULL;

    cJSON *parent = kind == JSON_TABLE_ARRAY ? cJSON_CreateArray() : cJSON_CreateObject();
    if (!parent) {
        *error = "系统内存不足";
        return NULL;
    }

    if (kind == JSON_TABLE_ARRAY) {
        size_t length = lua_rawlen(L, index);
        for (size_t i = 1; i <= length; ++i) {
            lua_rawgeti(L, index, (lua_Integer)i);
            cJSON *child = lua_to_cjson(L, -1, depth + 1, nodes, error);
            lua_pop(L, 1);
            if (!child || !cJSON_AddItemToArray(parent, child)) {
                if (child) cJSON_Delete(child);
                cJSON_Delete(parent);
                if (!*error) *error = "系统内存不足";
                return NULL;
            }
        }
        return parent;
    }

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        size_t key_length = 0;
        const char *key = lua_tolstring(L, -2, &key_length);
        if (!key || memchr(key, '\0', key_length) ||
            !passport_text_utf8_is_valid(key, key_length)) {
            lua_pop(L, 2);
            cJSON_Delete(parent);
            *error = "JSON 对象键必须是有效 UTF-8 且不能包含 NUL";
            return NULL;
        }
        cJSON *child = lua_to_cjson(L, -1, depth + 1, nodes, error);
        if (!child || !cJSON_AddItemToObject(parent, key, child)) {
            if (child) cJSON_Delete(child);
            lua_pop(L, 2);
            cJSON_Delete(parent);
            if (!*error) *error = "系统内存不足";
            return NULL;
        }
        lua_pop(L, 1);
    }
    return parent;
}

static cJSON *lua_to_cjson(lua_State *L, int index, size_t depth,
                           size_t *nodes, const char **error)
{
    int type = lua_type(L, index);
    if (depth > PASSPORT_JSON_MAX_DEPTH) {
        *error = "Lua table 嵌套超过 12 层";
        return NULL;
    }
    if (type == LUA_TTABLE && depth >= PASSPORT_JSON_MAX_DEPTH) {
        *error = "Lua table 嵌套超过 12 层";
        return NULL;
    }
    if (++(*nodes) > PASSPORT_JSON_MAX_NODES) {
        *error = "Lua 值超过 128 个";
        return NULL;
    }

    switch (type) {
    case LUA_TNIL:
        return cJSON_CreateNull();
    case LUA_TBOOLEAN:
        return cJSON_CreateBool(lua_toboolean(L, index));
    case LUA_TNUMBER:
        if (lua_isinteger(L, index)) {
            lua_Integer value = lua_tointeger(L, index);
            if (!number_is_safe((double)value)) {
                *error = "Lua 整数超出 JSON 安全范围";
                return NULL;
            }
            return cJSON_CreateNumber((double)value);
        } else {
            double value = (double)lua_tonumber(L, index);
            if (!number_is_safe(value)) {
                *error = "Lua 数字不是有限安全数值";
                return NULL;
            }
            return cJSON_CreateNumber(value);
        }
    case LUA_TSTRING: {
        size_t length = 0;
        const char *value = lua_tolstring(L, index, &length);
        if (memchr(value, '\0', length) ||
            !passport_text_utf8_is_valid(value, length)) {
            *error = "Lua 字符串必须是有效 UTF-8 且不能包含 NUL";
            return NULL;
        }
        return cJSON_CreateString(value);
    }
    case LUA_TTABLE:
        return table_to_cjson(L, index, depth, nodes, error);
    case LUA_TLIGHTUSERDATA:
        if (lua_touserdata(L, index) == (void *)&s_json_null_marker) {
            return cJSON_CreateNull();
        }
        break;
    default:
        break;
    }
    *error = "JSON 不支持该 Lua 类型";
    return NULL;
}

static int l_json_decode(lua_State *L)
{
    if (lua_type(L, 1) != LUA_TSTRING) return push_error(L, "decode 参数必须是字符串");
    size_t length = 0;
    const char *json = lua_tolstring(L, 1, &length);
    const char *error = NULL;
    if (!input_is_safe(json, length, &error)) return push_error(L, error);

    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length + 1, &parse_end, true);
    if (!root) return push_error(L, "JSON 语法无效");

    size_t nodes = 0;
    if (!cjson_tree_is_safe(root, 0, &nodes, &error) ||
        !push_cjson_value(L, root, 0, &error)) {
        cJSON_Delete(root);
        return push_error(L, error ? error : "JSON 解码失败");
    }
    cJSON_Delete(root);
    lua_pushnil(L);
    return 2;
}

static int l_json_encode(lua_State *L)
{
    if (lua_gettop(L) < 1) return push_error(L, "encode 缺少参数");
    const char *error = NULL;
    size_t nodes = 0;
    cJSON *root = lua_to_cjson(L, 1, 0, &nodes, &error);
    if (!root) return push_error(L, error ? error : "JSON 编码失败");

    /* cJSON asks callers to provide five bytes beyond the rendered length. */
    char *buffer = malloc(PASSPORT_JSON_MAX_BYTES + 5U);
    if (!buffer) {
        cJSON_Delete(root);
        return push_error(L, "系统内存不足");
    }
    bool printed = cJSON_PrintPreallocated(
        root, buffer, (int)PASSPORT_JSON_MAX_BYTES + 5, false);
    cJSON_Delete(root);
    if (!printed || strlen(buffer) > PASSPORT_JSON_MAX_BYTES) {
        free(buffer);
        return push_error(L, "JSON 输出超过 4096 字节");
    }
    lua_pushstring(L, buffer);
    free(buffer);
    lua_pushnil(L);
    return 2;
}

static int l_json_array(lua_State *L)
{
    if (lua_isnoneornil(L, 1)) {
        lua_newtable(L);
    } else if (lua_istable(L, 1)) {
        if (lua_getmetatable(L, 1)) {
            lua_rawgetp(L, LUA_REGISTRYINDEX, &s_json_array_metatable_key);
            bool is_array_marker = lua_rawequal(L, -1, -2);
            lua_pop(L, 2);
            if (!is_array_marker) return push_error(L, "array 不能覆盖已有 metatable");
        }
        lua_pushvalue(L, 1);
    } else {
        return push_error(L, "array 参数必须是 table 或 nil");
    }
    mark_array(L, -1);
    return 1;
}

void passport_runtime_register_json(lua_State *L)
{
    lua_newtable(L);
    lua_pushliteral(L, "passport.json.array");
    lua_setfield(L, -2, "__metatable");
    lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &s_json_array_metatable_key);
    lua_pop(L, 1);

    static const luaL_Reg functions[] = {
        {"decode", l_json_decode},
        {"encode", l_json_encode},
        {"array", l_json_array},
        {NULL, NULL},
    };
    luaL_newlib(L, functions);
    lua_pushlightuserdata(L, (void *)&s_json_null_marker);
    lua_setfield(L, -2, "null");
}
