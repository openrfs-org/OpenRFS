/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: address translation.
 * Everything a driver hands to a device lies in the identity-mapped DMA
 * arena, so a virtual address is the bus address, as in SeaBIOS.
 */
#ifndef OPENRFS_SEABIOS_MEMMAP_H
#define OPENRFS_SEABIOS_MEMMAP_H

#include "types.h"

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

static inline openrfs_seabios_uintptr virt_to_phys(void *v) {
    return (openrfs_seabios_uintptr)v;
}

#endif
