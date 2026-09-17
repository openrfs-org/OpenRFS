/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  OpenRFS timer backend addition for SDL 2.32.10. This file is distributed
  under the same zlib license as SDL.
*/

#include "../../SDL_internal.h"

#ifdef SDL_TIMER_OPENRFS

#include "SDL_timer.h"

#include <openrfs/runtime.h>

static Uint64 openrfs_ticks_origin;
static SDL_bool openrfs_ticks_started = SDL_FALSE;

void SDL_TicksInit(void)
{
    if (!openrfs_ticks_started) {
        openrfs_ticks_origin = openrfs_monotonic_ns();
        openrfs_ticks_started = SDL_TRUE;
    }
}

void SDL_TicksQuit(void)
{
    openrfs_ticks_started = SDL_FALSE;
    openrfs_ticks_origin = 0U;
}

Uint64 SDL_GetTicks64(void)
{
    Uint64 now;

    if (!openrfs_ticks_started) {
        SDL_TicksInit();
    }
    now = openrfs_monotonic_ns();
    return now >= openrfs_ticks_origin ?
        (now - openrfs_ticks_origin) / UINT64_C(1000000) : 0U;
}

Uint64 SDL_GetPerformanceCounter(void)
{
    return openrfs_monotonic_ns();
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return UINT64_C(1000000000);
}

void SDL_Delay(Uint32 milliseconds)
{
    const Uint64 now = openrfs_monotonic_ns();
    const Uint64 delta = (Uint64)milliseconds * UINT64_C(1000000);
    const Uint64 deadline = delta > UINT64_MAX - now ? UINT64_MAX :
        now + delta;

    (void)openrfs_sleep_until(deadline);
}

#endif /* SDL_TIMER_OPENRFS */
