/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * RSD environment for the vendored SeaBIOS drivers: address translation.
 * Everything a driver hands to a device lies in the identity-mapped DMA
 * arena, so a virtual address is the bus address, as in SeaBIOS.
 */
#ifndef RSD_SEABIOS_MEMMAP_H
#define RSD_SEABIOS_MEMMAP_H

#include "types.h"

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

static inline rsd_seabios_uintptr virt_to_phys(void *v) {
    return (rsd_seabios_uintptr)v;
}

#endif
