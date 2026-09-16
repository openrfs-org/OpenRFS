/* SPDX-License-Identifier: GPL-3.0-only */

#include "lua.h"

#include <opengat/runtime.h>

#include <stdint.h>
#include <stdio.h>

static uint64_t startup_started;

void opengat_application_entry_probe(void);
void __real_luaL_openlibs(lua_State *state);
void __wrap_luaL_openlibs(lua_State *state);

void opengat_application_entry_probe(void)
{
    startup_started = opengat_monotonic_ns();
}

void __wrap_luaL_openlibs(lua_State *state)
{
    __real_luaL_openlibs(state);
    printf("OPENGAT PERF lua startup_ns=%llu\n",
        (unsigned long long)(opengat_monotonic_ns() - startup_started));
}
