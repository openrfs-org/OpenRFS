/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: memory.
 *
 * SeaBIOS sorts allocations into zones by where real-mode code can reach
 * them and by whether they survive POST. Neither distinction exists here.
 * Every zone is the same DMA arena: contiguous, identity-mapped, below
 * 4 GiB, zeroed on allocation and owned by the devices this layer binds,
 * which is what the drivers assume when they write an allocation's address
 * into a device register.
 */
#ifndef OPENRFS_SEABIOS_MALLOC_H
#define OPENRFS_SEABIOS_MALLOC_H

#include "types.h"

#define MALLOC_MIN_ALIGN 16

/* The zones exist only as names; nvme.c passes them to _malloc directly. */
struct zone_s { int unused; };
extern struct zone_s ZoneLow, ZoneHigh, ZoneFSeg, ZoneTmpLow, ZoneTmpHigh;

void *openrfs_seabios_memalign(u32 align, u32 size);
void *_malloc(struct zone_s *zone, u32 size, u32 align);
void free(void *data);

static inline void *malloc_low(u32 size) {
    return openrfs_seabios_memalign(MALLOC_MIN_ALIGN, size);
}
static inline void *malloc_high(u32 size) {
    return openrfs_seabios_memalign(MALLOC_MIN_ALIGN, size);
}
static inline void *malloc_fseg(u32 size) {
    return openrfs_seabios_memalign(MALLOC_MIN_ALIGN, size);
}
static inline void *malloc_tmplow(u32 size) {
    return openrfs_seabios_memalign(MALLOC_MIN_ALIGN, size);
}
static inline void *malloc_tmphigh(u32 size) {
    return openrfs_seabios_memalign(MALLOC_MIN_ALIGN, size);
}
static inline void *malloc_tmp(u32 size) {
    return openrfs_seabios_memalign(MALLOC_MIN_ALIGN, size);
}
static inline void *memalign_low(u32 align, u32 size) {
    return openrfs_seabios_memalign(align, size);
}
static inline void *memalign_high(u32 align, u32 size) {
    return openrfs_seabios_memalign(align, size);
}
static inline void *memalign_tmplow(u32 align, u32 size) {
    return openrfs_seabios_memalign(align, size);
}
static inline void *memalign_tmphigh(u32 align, u32 size) {
    return openrfs_seabios_memalign(align, size);
}
static inline void *memalign_tmp(u32 align, u32 size) {
    return openrfs_seabios_memalign(align, size);
}

#endif
