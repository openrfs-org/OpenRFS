/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Heap interface for iPXE drivers (include/stdlib.h). Every allocation comes
 * from the layer's DMA arena, so any buffer a driver hands to its device is
 * already inside memory the device was granted.
 */
#ifndef OPENRFS_IPXE_STDLIB_H
#define OPENRFS_IPXE_STDLIB_H

#include <stddef.h>
#include <stdint.h>

void *malloc(size_t size) __attribute__((malloc));
void *zalloc(size_t size) __attribute__((malloc));
void *realloc(void *old_pointer, size_t new_size);
void free(void *pointer);
void zfree(void *pointer);
long int random(void);
void srandom(unsigned int seed);
unsigned long strtoul(const char *text, char **end, int base);

static inline int abs(int value)
{
    return value < 0 ? -value : value;
}

#endif
