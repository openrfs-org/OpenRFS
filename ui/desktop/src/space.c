/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd_desktop/space.h>

#include "rsd_wallpaper.h"

/*
 * THE WALLPAPER, BLITTED OUT OF A TABLE.
 *
 * It is the owner's picture. There is no filesystem under this desktop
 * and no image decoder in it, so the picture cannot be a file it opens -
 * it has to be a table compiled in, and src/rsd_wallpaper.h is that
 * table. tools/make-wallpaper-table.py builds it; replace the JPEG in
 * assets/wallpaper/ and re-run that script and the desktop picks it up.
 *
 * Run-length encoded, because most rows of a night sky are one colour
 * for a long way. A 1280x800 image at four bytes a pixel is four
 * megabytes; this is about a third of one.
 *
 * UNDITHERED, which is the decision worth knowing about. Dithering is
 * the obvious way to quantise a smooth gradient and it is exactly wrong
 * in front of a run-length encoder: it replaces long flat runs with
 * per-pixel noise, and noise does not compress at all. Banding in the
 * sky costs less than doubling the table.
 *
 * This decoder holds no state. It walks the runs once, in order, and
 * writes through rsd_surface_plot. Source pixels are mapped to the current
 * surface size so the wordmark stays visible on the kernel's 1024x768 mode.
 */
void rsd_space_draw(struct rsd_surface *surface, struct rsd_rect clip)
{
    uint32_t run;
    uint32_t x = 0U;
    uint32_t y = 0U;

    if (!rsd_surface_valid(surface)) {
        return;
    }
    for (run = 0U; run < RSD_WALLPAPER_RUNS; ++run) {
        uint32_t left = rsd_wallpaper_run[run];
        uint32_t colour = rsd_wallpaper_ink[rsd_wallpaper_at[run]];

        while (left > 0U) {
            uint32_t x0 = (uint32_t)((uint64_t)x * surface->width /
                RSD_WALLPAPER_WIDTH);
            uint32_t x1 = (uint32_t)((uint64_t)(x + 1U) * surface->width /
                RSD_WALLPAPER_WIDTH);
            uint32_t y0 = (uint32_t)((uint64_t)y * surface->height /
                RSD_WALLPAPER_HEIGHT);
            uint32_t y1 = (uint32_t)((uint64_t)(y + 1U) * surface->height /
                RSD_WALLPAPER_HEIGHT);

            for (uint32_t target_y = y0; target_y < y1; ++target_y) {
                for (uint32_t target_x = x0; target_x < x1; ++target_x) {
                    rsd_surface_plot(surface, clip, target_x, target_y,
                        colour);
                }
            }
            --left;
            ++x;
            if (x >= RSD_WALLPAPER_WIDTH) {
                x = 0U;
                ++y;
                if (y >= RSD_WALLPAPER_HEIGHT) {
                    return;
                }
            }
        }
    }
}
