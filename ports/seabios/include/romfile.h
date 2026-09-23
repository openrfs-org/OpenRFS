/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: firmware files.
 * OpenRFS lends the drivers no CBFS or fw_cfg files, so every lookup misses
 * and every integer takes the caller's default.
 */
#ifndef OPENRFS_SEABIOS_ROMFILE_H
#define OPENRFS_SEABIOS_ROMFILE_H

#include "types.h"

struct romfile_s {
    struct romfile_s *next;
    char name[128];
    u32 size;
    int (*copy)(struct romfile_s *file, void *dest, u32 maxlen);
};
struct romfile_s *romfile_findprefix(const char *prefix, struct romfile_s *prev);
struct romfile_s *romfile_find(const char *name);
u64 romfile_loadint(const char *name, u64 defval);

#endif
