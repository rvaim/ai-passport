#include "passport_runtime_json.h"

#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s TEST_SCRIPT [SCRIPT_ARGS...]\n", argv[0]);
        return 2;
    }
    lua_State *L = luaL_newstate();
    if (!L) return 1;
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L, 1);

    lua_newtable(L);
    passport_runtime_register_json(L);
    lua_setfield(L, -2, "json");
    lua_setglobal(L, "passport");

    lua_createtable(L, argc - 1, 0);
    for (int i = 1; i < argc; ++i) {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i - 1);
    }
    lua_setglobal(L, "arg");

    int status = luaL_dofile(L, argv[1]);
    if (status != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    lua_close(L);
    return 0;
}
