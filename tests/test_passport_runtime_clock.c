#include "passport_runtime_clock.h"

#include "lauxlib.h"
#include "lualib.h"
#include <stdint.h>
#include <stdio.h>

static int64_t s_monotonic_us;

int64_t esp_timer_get_time(void)
{
    return s_monotonic_us;
}

static int run(lua_State *L, const char *script)
{
    int status = luaL_dostring(L, script);
    if (status == LUA_OK) return 0;
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
    return 1;
}

int main(void)
{
    lua_State *L = luaL_newstate();
    if (!L) return 1;
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1); lua_pop(L, 1);
    lua_newtable(L);
    passport_runtime_clock_register(L);
    lua_setfield(L, -2, "clock");
    lua_setglobal(L, "passport");

    if (run(L,
            "assert(passport.clock.valid() == false)\n"
            "local value, err = passport.clock.now()\n"
            "assert(value == nil and type(err) == 'string')\n"
            "for _, bad in ipairs({'', '1704067199', '253402300800', "
            "'170406720x', '0000000000'}) do\n"
            "  local ok, why = passport.clock.sync(bad)\n"
            "  assert(ok == false and type(why) == 'string')\n"
            "end\n"
            "assert(pcall(passport.clock.sync, 1704067200) == false)\n")) {
        lua_close(L);
        return 1;
    }

    s_monotonic_us = INT64_C(10000000);
    if (run(L,
            "local ok, err = passport.clock.sync('1730000000')\n"
            "assert(ok == true and err == nil)\n"
            "assert(passport.clock.valid() == true)\n"
            "local now, why = passport.clock.now()\n"
            "assert(now == '1730000000' and why == nil)\n")) {
        lua_close(L);
        return 1;
    }

    s_monotonic_us += INT64_C(30999999);
    if (run(L, "assert(passport.clock.now() == '1730000030')\n")) {
        lua_close(L);
        return 1;
    }

    s_monotonic_us = 0;
    if (run(L,
            "assert(passport.clock.valid() == false)\n"
            "local now, err = passport.clock.now()\n"
            "assert(now == nil and type(err) == 'string')\n")) {
        lua_close(L);
        return 1;
    }

    s_monotonic_us = INT64_C(50000000);
    if (run(L,
            "assert(passport.clock.sync('253402300799') == true)\n"
            "assert(passport.clock.now() == '253402300799')\n")) {
        lua_close(L);
        return 1;
    }
    s_monotonic_us += INT64_C(1000000);
    if (run(L, "assert(passport.clock.valid() == false)\n")) {
        lua_close(L);
        return 1;
    }

    lua_close(L);
    puts("Passport clock API host tests: PASS");
    return 0;
}
