/* SPDX-License-Identifier: GPL-3.0-only */
/* iPXE's profiler (include/ipxe/profile.h) compiled out, as in its builds. */
#ifndef OPENRFS_IPXE_PROFILE_H
#define OPENRFS_IPXE_PROFILE_H

#include <stdint.h>

struct profiler {
    const char *name;
    unsigned long started;
    unsigned long stopped;
};

#define __profiler __attribute__((unused))

static inline void profile_start(struct profiler *profiler)
{
    (void)profiler;
}

static inline void profile_stop(struct profiler *profiler)
{
    (void)profiler;
}

static inline void profile_exclude(struct profiler *profiler)
{
    (void)profiler;
}

static inline void profile_start_at(struct profiler *profiler,
    unsigned long started)
{
    (void)profiler;
    (void)started;
}

static inline void profile_stop_at(struct profiler *profiler,
    unsigned long stopped)
{
    (void)profiler;
    (void)stopped;
}

static inline void profile_custom(struct profiler *profiler,
    unsigned long sample)
{
    (void)profiler;
    (void)sample;
}

#endif
