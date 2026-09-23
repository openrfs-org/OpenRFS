/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: segment access.
 *
 * These are the definitions SeaBIOS's farptr.h itself uses when a file is
 * compiled for 32-bit flat mode, where every "far" access is an ordinary
 * one and the stack segment's base is zero.
 */
#ifndef OPENRFS_SEABIOS_FARPTR_H
#define OPENRFS_SEABIOS_FARPTR_H

#include "types.h"
#include "x86.h"

/*
 * A far access in flat mode reads real-mode memory (the IVT and BDA at
 * physical 0x0-0x4FF). OpenRFS never lends that memory to a driver:
 * biosvar.h redirects the BDA to a private copy, and any other far access
 * fails at link time instead of touching low memory.
 */
extern void openrfs_seabios_far_access_unsupported(void) __noreturn;
#define GET_FARVAR(seg, var) \
    (openrfs_seabios_far_access_unsupported(), (var))
#define SET_FARVAR(seg, var, val) \
    do { openrfs_seabios_far_access_unsupported(); (void)(val); } while (0)
#define GET_VAR(seg, var) (var)
#define SET_VAR(seg, var, val) do { (var) = (val); } while (0)
#define SET_SEG(SEG, value) ((void)(value))
#define GET_SEG(SEG) 0
#define GET_FLATPTR(ptr) (ptr)
#define SET_FLATPTR(ptr, val) do { (ptr) = (val); } while (0)

#define insb_fl(port, ptr_fl, count) insb(port, ptr_fl, count)
#define insw_fl(port, ptr_fl, count) insw(port, ptr_fl, count)
#define insl_fl(port, ptr_fl, count) insl(port, ptr_fl, count)
#define outsb_fl(port, ptr_fl, count) outsb(port, ptr_fl, count)
#define outsw_fl(port, ptr_fl, count) outsw(port, ptr_fl, count)
#define outsl_fl(port, ptr_fl, count) outsl(port, ptr_fl, count)

#define FLATPTR_TO_SEG(p) (((u32)(openrfs_seabios_uintptr)(p)) >> 4)
#define FLATPTR_TO_OFFSET(p) (((u32)(openrfs_seabios_uintptr)(p)) & 0xf)
#define MAKE_FLATPTR(seg,off) \
    ((void*)((((openrfs_seabios_uintptr)(seg))<<4) + \
        (openrfs_seabios_uintptr)(off)))

#define SEGOFF(s,o) ({struct segoff_s __so; __so.offset=(o); __so.seg=(s); __so;})

static inline struct segoff_s FLATPTR_TO_SEGOFF(void *p)
{
    return SEGOFF(FLATPTR_TO_SEG(p), FLATPTR_TO_OFFSET(p));
}

static inline void *SEGOFF_TO_FLATPTR(struct segoff_s so)
{
    return MAKE_FLATPTR(so.seg, so.offset);
}

#endif
