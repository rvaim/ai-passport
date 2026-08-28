#pragma once

#include "lua.h"

/**
 * Push the passport.clock table.
 *
 * The clock is volatile and shared by PAP runtimes while the firmware remains
 * powered. A browser or another trusted time source must call sync after boot.
 */
void passport_runtime_clock_register(lua_State *L);
