#pragma once

#include "lua.h"

/** Push the bounded Passport JSON API table onto the Lua stack. */
void passport_runtime_register_json(lua_State *L);
