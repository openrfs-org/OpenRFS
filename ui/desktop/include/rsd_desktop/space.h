/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_SPACE_H
#define RSD_SPACE_H

#include <rsd_desktop/surface.h>

/*
 * The desktop's ground, drawn rather than loaded.
 *
 * It replaces the X root weave, which was right for a bare OpenBSD
 * session and is not right for this one: a stipple is what a screen
 * shows when nothing has painted it, and something has.
 *
 * Costs one pass over the framebuffer and ships nothing beside the
 * kernel - see the comment at the top of src/space.c for why a PNG was
 * not the answer.
 */
void rsd_space_draw(struct rsd_surface *surface, struct rsd_rect clip);

#endif /* RSD_SPACE_H */
