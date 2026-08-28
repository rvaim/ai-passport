#include "passport_runtime_clock.h"

#include "esp_timer.h"
#include "lauxlib.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define CLOCK_MIN_UNIX_SECONDS UINT64_C(1704067200) /* 2024-01-01 UTC. */
#define CLOCK_MAX_UNIX_SECONDS UINT64_C(253402300799) /* 9999-12-31 UTC. */

typedef struct {
    uint64_t base_epoch;
    int64_t base_monotonic_us;
    bool valid;
} volatile_clock_t;

/*
 * The ESP32-C3 has no battery-backed wall clock. Keep one volatile epoch for
 * the powered firmware session; a reboot or power loss intentionally makes it
 * invalid instead of returning a plausible but incorrect timestamp.
 */
static volatile_clock_t s_clock;

static bool parse_epoch(const char *text, size_t length, uint64_t *out)
{
    if (!text || !out || length < 10U || length > 12U) return false;
    uint64_t value = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10U + (uint64_t)(text[i] - '0');
    }
    if (value < CLOCK_MIN_UNIX_SECONDS || value > CLOCK_MAX_UNIX_SECONDS) {
        return false;
    }
    *out = value;
    return true;
}

static bool clock_now(uint64_t *out)
{
    if (!out || !s_clock.valid) return false;
    int64_t elapsed = esp_timer_get_time() - s_clock.base_monotonic_us;
    if (elapsed < 0) {
        s_clock.valid = false;
        return false;
    }
    uint64_t elapsed_seconds = (uint64_t)(elapsed / 1000000);
    if (elapsed_seconds > CLOCK_MAX_UNIX_SECONDS - s_clock.base_epoch) {
        s_clock.valid = false;
        return false;
    }
    *out = s_clock.base_epoch + elapsed_seconds;
    return true;
}

static int l_clock_sync(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TSTRING);
    size_t length = 0;
    const char *text = lua_tolstring(L, 1, &length);
    uint64_t epoch = 0;
    if (!parse_epoch(text, length, &epoch)) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "Unix 时间必须是 2024..9999 年的十进制秒字符串");
        return 2;
    }
    s_clock.base_epoch = epoch;
    s_clock.base_monotonic_us = esp_timer_get_time();
    s_clock.valid = true;
    lua_pushboolean(L, 1);
    lua_pushnil(L);
    return 2;
}

static int l_clock_valid(lua_State *L)
{
    uint64_t ignored = 0;
    lua_pushboolean(L, clock_now(&ignored));
    return 1;
}

static int l_clock_now(lua_State *L)
{
    uint64_t epoch = 0;
    if (!clock_now(&epoch)) {
        lua_pushnil(L);
        lua_pushstring(L, "设备时间尚未同步");
        return 2;
    }
    char text[24];
    snprintf(text, sizeof(text), "%" PRIu64, epoch);
    lua_pushstring(L, text);
    lua_pushnil(L);
    return 2;
}

void passport_runtime_clock_register(lua_State *L)
{
    static const luaL_Reg functions[] = {
        {"sync", l_clock_sync}, {"valid", l_clock_valid},
        {"now", l_clock_now}, {NULL, NULL},
    };
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
}
