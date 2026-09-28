/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_PNG_H
#define RSD_PNG_H

#include <stddef.h>
#include <stdint.h>

/* Writes 0x00RRGGBB pixels as a truecolour PNG.  Returns non-zero on
 * success.  Host-only: the shell itself never encodes anything. */
int png_write(const char *path, const uint32_t *pixels, uint32_t width,
    uint32_t height);

#endif /* RSD_PNG_H */
