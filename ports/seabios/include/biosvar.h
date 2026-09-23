/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: BIOS variables.
 *
 * Globals are ordinary variables in flat mode. The BIOS Data Area is not the
 * machine's: the few fields the drivers keep there (the floppy controller's
 * recalibration and motor state, the disk control byte) live in a private
 * copy the glue owns, so no driver writes physical page zero.
 */
#ifndef OPENRFS_SEABIOS_BIOSVAR_H
#define OPENRFS_SEABIOS_BIOSVAR_H

#include "config.h"
#include "farptr.h"
#include "std/bda.h"
#include "types.h"

extern struct bios_data_area_s openrfs_seabios_bda;

#define GET_BDA(var) (openrfs_seabios_bda.var)
#define SET_BDA(var, val) do { openrfs_seabios_bda.var = (val); } while (0)

/* Nothing here installs real-mode vectors; the value is discarded. */
#define SET_IVT(vector, segoff) do { (void)(vector); (void)(segoff); } while (0)
#define FUNC16(func) SEGOFF(0, 0)

#define SEG_LOW 0

#define GET_GLOBAL(var) (var)
#define SET_GLOBAL(var, val) do { (var) = (val); } while (0)
#define GET_GLOBALFLAT(var) (var)
#define SET_GLOBALFLAT(var, val) do { (var) = (val); } while (0)
#define GET_LOW(var) (var)
#define SET_LOW(var, val) do { (var) = (val); } while (0)
#define GET_LOWFLAT(var) (var)
#define SET_LOWFLAT(var, val) do { (var) = (val); } while (0)

#endif
