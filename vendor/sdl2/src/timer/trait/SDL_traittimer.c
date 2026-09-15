/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  Trait OS timer backend addition for SDL 2.32.10. This file is distributed
  under the same zlib license as SDL.
*/

#include "../../SDL_internal.h"

#ifdef SDL_TIMER_TRAIT

#include "SDL_timer.h"

#include <trait/runtime.h>

static Uint64 trait_ticks_origin;
static SDL_bool trait_ticks_started = SDL_FALSE;

void SDL_TicksInit(void)
{
    if (!trait_ticks_started) {
        trait_ticks_origin = trait_monotonic_ns();
        trait_ticks_started = SDL_TRUE;
    }
}

void SDL_TicksQuit(void)
{
    trait_ticks_started = SDL_FALSE;
    trait_ticks_origin = 0U;
}

Uint64 SDL_GetTicks64(void)
{
    Uint64 now;

    if (!trait_ticks_started) {
        SDL_TicksInit();
    }
    now = trait_monotonic_ns();
    return now >= trait_ticks_origin ?
        (now - trait_ticks_origin) / UINT64_C(1000000) : 0U;
}

Uint64 SDL_GetPerformanceCounter(void)
{
    return trait_monotonic_ns();
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return UINT64_C(1000000000);
}

void SDL_Delay(Uint32 milliseconds)
{
    const Uint64 now = trait_monotonic_ns();
    const Uint64 delta = (Uint64)milliseconds * UINT64_C(1000000);
    const Uint64 deadline = delta > UINT64_MAX - now ? UINT64_MAX :
        now + delta;

    (void)trait_sleep_until(deadline);
}

#endif /* SDL_TIMER_TRAIT */
