/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: string routines,
 * with SeaBIOS's names and signatures. The "_fl" (flat pointer) variants are
 * the plain ones in flat mode.
 */
#ifndef OPENRFS_SEABIOS_STRING_H
#define OPENRFS_SEABIOS_STRING_H

#include "types.h"

u8 checksum(void *buf, u32 len);
size_t strlen(const char *s);
int memcmp(const void *s1, const void *s2, size_t n);
int strcmp(const char *s1, const char *s2);
void *memset(void *s, int c, size_t n);
void memset_fl(void *ptr, u8 val, size_t size);
void memset16_fl(void *ptr, u16 val, size_t size);
void memcpy_fl(void *d_fl, const void *s_fl, size_t len);
/* Segments are zero in flat mode (GET_SEG and SEG_LOW), so this is memcpy. */
void memcpy_far(u16 d_seg, void *d_far, u16 s_seg, const void *s_far,
                size_t len);
void *memcpy(void *d1, const void *s1, size_t len);
#define memcpy __builtin_memcpy
void iomemcpy(void *d, const void *s, u32 len);
void *memmove(void *d, const void *s, size_t len);
char *strtcpy(char *dest, const char *src, size_t len);
char *strchr(const char *s, int c);
char *nullTrailingSpace(char *buf);

#endif
