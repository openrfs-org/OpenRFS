/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Physically contiguous allocation for iPXE drivers (include/ipxe/malloc.h).
 * Served from the layer's DMA arena; see ports/ipxe/ipxe_glue.c.
 */
#ifndef OPENRFS_IPXE_MALLOC_H
#define OPENRFS_IPXE_MALLOC_H

#include <stddef.h>
#include <stdlib.h>

void *malloc_phys_offset(size_t size, size_t physical_align,
    size_t offset) __attribute__((malloc));
void *malloc_phys(size_t size, size_t physical_align)
    __attribute__((malloc));
void free_phys(void *pointer, size_t size);

#endif
