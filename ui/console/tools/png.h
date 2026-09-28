/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_PNG_H
#define RSD_PNG_H
#include <stdint.h>
/* Writes 8-bit RGB. Returns 0 on success. */
int png_write(const char *path, const uint8_t *rgb, uint32_t w, uint32_t h);
#endif
