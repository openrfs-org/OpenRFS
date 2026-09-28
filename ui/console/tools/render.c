/* SPDX-License-Identifier: GPL-3.0-only */
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "png.h"
#include "../src/rsd_font.h"

/*
 * Sixteen colours, because the attribute byte has room for sixteen.
 *
 * Slot 0, 7 and 15 are the console's own: one phosphor on a ground that
 * is very slightly blue-black, the way a CRT never quite reached black,
 * and a brighter version of the same phosphor rather than a second hue.
 * Text the shell writes only ever lands in those three.
 *
 * The other thirteen are the IBM text palette, which is what a console
 * the installer draws its boxes in.
 *
 * Two of them are not IBM's. Red is #9E1B1B and bright red is #E8564B,
 * which are the two reds the logo is drawn in and the two the website
 * sets as --deep and its dark-mode link. The installer paints its field
 * red, and three different programs painting three slightly different
 * reds is how a brand stops being one.
 */
static const uint8_t PALETTE[16][3] = {
    { 0x0B, 0x0D, 0x12 },   /*  0 black - the console ground */
    { 0x00, 0x00, 0xAA },   /*  1 blue - the field behind the dialogs */
    { 0x00, 0xAA, 0x00 },   /*  2 green */
    { 0x00, 0xAA, 0xAA },   /*  3 cyan */
    { 0x9E, 0x1B, 0x1B },   /*  4 red - the brand's, not the IBM one */
    { 0xAA, 0x00, 0xAA },   /*  5 magenta */
    { 0xAA, 0x55, 0x00 },   /*  6 brown */
    { 0xC8, 0xCE, 0xD4 },   /*  7 grey - the console ink */
    { 0x55, 0x55, 0x55 },   /*  8 dark grey */
    { 0x55, 0x55, 0xFF },   /*  9 bright blue */
    { 0x55, 0xFF, 0x55 },   /* 10 bright green */
    { 0x55, 0xFF, 0xFF },   /* 11 bright cyan */
    { 0xE8, 0x56, 0x4B },   /* 12 bright red - the hotkey letter */
    { 0xFF, 0x55, 0xFF },   /* 13 bright magenta */
    { 0xFF, 0xFF, 0x55 },   /* 14 yellow */
    { 0xFF, 0xFF, 0xFF },   /* 15 white - the console's bright ink */
};

int rsd_render(const struct rsd_term *t, bool cursor, const char *path)
{
    uint32_t w = RSD_COLS * RSD_CELL_W;
    uint32_t h = RSD_ROWS * RSD_CELL_H;
    uint8_t *rgb;
    uint32_t r, c, y, x;
    int rc;

    if (t == NULL) {
        return -1;
    }
    rgb = malloc((size_t)w * h * 3U);
    if (rgb == NULL) {
        return -1;
    }
    for (r = 0U; r < RSD_ROWS; ++r) {
        for (c = 0U; c < RSD_COLS; ++c) {
            uint8_t ch = (uint8_t)t->cell[r][c];
            uint8_t attr = t->attr[r][c];
            const uint8_t *fg = PALETTE[RSD_ATTR_FG(attr)];
            const uint8_t *bg = PALETTE[RSD_ATTR_BG(attr)];
            /*
             * ONE CHARACTER GENERATOR, which is all a VGA text console
             * has. Bit 3 of the foreground is the intensity bit and
             * what it changes is the COLOUR - PALETTE's upper eight
             * are the bright half - so it is already accounted for
             * above. This used to pick between two font tables with
             * it, a plain one and a heavier one rasterised from the
             * bold cut of a typeface, which is a thing this hardware
             * could not do.
             */
            const uint8_t *glyph = NULL;

            if (ch >= RSD_GLYPH_FIRST && ch <= RSD_GLYPH_LAST) {
                glyph = rsd_font[ch - RSD_GLYPH_FIRST];
            }
            for (y = 0U; y < RSD_CELL_H; ++y) {
                for (x = 0U; x < RSD_CELL_W; ++x) {
                    size_t px = ((size_t)(r * RSD_CELL_H + y) * w
                                 + (c * RSD_CELL_W + x)) * 3U;
                    int ink = glyph != NULL
                              && (glyph[y] & (1U << x)) != 0U;
                    memcpy(&rgb[px], ink ? fg : bg, 3);
                }
            }
        }
    }
    /* A block cursor, because that is what a text console has. */
    if (cursor && t->row < RSD_ROWS && t->col < RSD_COLS) {
        const uint8_t *fg = PALETTE[RSD_ATTR_FG(t->attr[t->row][t->col])];

        for (y = 0U; y < RSD_CELL_H; ++y) {
            for (x = 0U; x < RSD_CELL_W; ++x) {
                size_t px = ((size_t)(t->row * RSD_CELL_H + y) * w
                             + (t->col * RSD_CELL_W + x)) * 3U;
                memcpy(&rgb[px], fg, 3);
            }
        }
    }
    /* The screenshots are committed, so they live beside the source
     * rather than in the build directory that gets swept away. */
    (void)mkdir("../screens", 0755);
    rc = png_write(path, rgb, w, h);
    if (rc == 0) {
        printf("wrote %s (%ux%u)\n", path, w, h);
    }
    free(rgb);
    return rc;
}
