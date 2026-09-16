/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  OpenGAT timer backend addition for SDL 2.32.10. This file is distributed
  under the same zlib license as SDL.
*/

#include "../../SDL_internal.h"

#ifdef SDL_TIMER_OPENGAT

#include "SDL_timer.h"

#include <opengat/runtime.h>

static Uint64 opengat_ticks_origin;
static SDL_bool opengat_ticks_started = SDL_FALSE;

void SDL_TicksInit(void)
{
    if (!opengat_ticks_started) {
        opengat_ticks_origin = opengat_monotonic_ns();
        opengat_ticks_started = SDL_TRUE;
    }
}

void SDL_TicksQuit(void)
{
    opengat_ticks_started = SDL_FALSE;
    opengat_ticks_origin = 0U;
}

Uint64 SDL_GetTicks64(void)
{
    Uint64 now;

    if (!opengat_ticks_started) {
        SDL_TicksInit();
    }
    now = opengat_monotonic_ns();
    return now >= opengat_ticks_origin ?
        (now - opengat_ticks_origin) / UINT64_C(1000000) : 0U;
}

Uint64 SDL_GetPerformanceCounter(void)
{
    return opengat_monotonic_ns();
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return UINT64_C(1000000000);
}

void SDL_Delay(Uint32 milliseconds)
{
    const Uint64 now = opengat_monotonic_ns();
    const Uint64 delta = (Uint64)milliseconds * UINT64_C(1000000);
    const Uint64 deadline = delta > UINT64_MAX - now ? UINT64_MAX :
        now + delta;

    (void)opengat_sleep_until(deadline);
}

#endif /* SDL_TIMER_OPENGAT */
