/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_GEARS_H
#define TRAIT_GEARS_H

#include <trait/surface.h>

/*
 * glxgears, which is a PROGRAM and therefore lives in a window.
 *
 * It was the root window for a while, which is not where it runs: you
 * type glxgears and a window comes up with the gears in it.  The
 * geometry is Brian Paul's, projected ahead of time into
 * src/trait_gears_art.h; this fills it into whatever rectangle it is
 * handed, so the same code serves a 320-pixel window and a screen.
 */
void trait_gears_draw(struct trait_surface *surface, struct trait_rect clip);

#endif /* TRAIT_GEARS_H */
