/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_RENDER_H
#define RSD_RENDER_H

#include <stdbool.h>

#include <rsd/term.h>

/*
 * Cells to pixels. This is the only file that knows a cell has a size,
 * which is why the shell and the installer can both be drawn by it
 * without either of them knowing what a pixel is.
 */
int rsd_render(const struct rsd_term *t, bool cursor, const char *path);

#endif /* RSD_RENDER_H */
