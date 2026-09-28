/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd_desktop/font.h>

#include "rsd_font_sans_11.h"
#include "rsd_font_sans_12.h"
#include "rsd_font_sans_14.h"

/*
 * THREE SIZES OF ONE FACE, RASTERISED AHEAD OF TIME.
 *
 * There is no font server behind a framebuffer and no scaler either, so
 * "a different font size" means a different set of bitmaps.  Settings
 * offers the three that exist; offering a slider over sizes that were
 * never generated would be a control that does nothing, and scaling one
 * set to stand in for the others is how text stops looking like text.
 *
 * THE FACE CHANGED, AND IT IS THE BIGGEST CHANGE IN THIS PASS.
 *
 * It was Misc-Fixed 6x13, 7x14 and 9x18: X11 bitmap fonts, one bit per
 * pixel, no shades and no curves.  Those are the right faces for an X
 * session of 1995 and they were the loudest thing on this screen saying
 * 1995.  No amount of gradient or rounding gets a window to read as 2010
 * while the words on it are a bitmap font - type is the first thing
 * anybody sees.
 *
 * So it is DejaVu Sans now, antialiased, at the size KDE 4 came up in:
 * its default UI font was "Sans Serif 9", fontconfig resolved that to
 * DejaVu Sans on every distribution that shipped KDE 4, and 9 points at
 * 96 dpi is a 12-pixel em.  That is the middle size here and the one a
 * session comes up in.
 *
 * Coverage rather than bits: every byte in the tables is 0..255, so the
 * text is antialiased everywhere the shell draws it.
 */
struct rsd_font_face {
    const struct rsd_glyph *glyphs;
    uint32_t count;
    uint32_t first;
    uint32_t ascent;
    uint32_t height;
    uint32_t pixels;
    const char *name;
};

static const struct rsd_font_face FACES[] = {
    { (const struct rsd_glyph *)rsd_font_sans_11,
      sizeof(rsd_font_sans_11) / sizeof(rsd_font_sans_11[0]),
      RSD_FONT_SANS_11_FIRST, RSD_FONT_SANS_11_ASCENT,
      RSD_FONT_SANS_11_HEIGHT, RSD_FONT_SANS_11_EM,
      RSD_FONT_SANS_11_NAME },
    { (const struct rsd_glyph *)rsd_font_sans_12,
      sizeof(rsd_font_sans_12) / sizeof(rsd_font_sans_12[0]),
      RSD_FONT_SANS_12_FIRST, RSD_FONT_SANS_12_ASCENT,
      RSD_FONT_SANS_12_HEIGHT, RSD_FONT_SANS_12_EM,
      RSD_FONT_SANS_12_NAME },
    { (const struct rsd_glyph *)rsd_font_sans_14,
      sizeof(rsd_font_sans_14) / sizeof(rsd_font_sans_14[0]),
      RSD_FONT_SANS_14_FIRST, RSD_FONT_SANS_14_ASCENT,
      RSD_FONT_SANS_14_HEIGHT, RSD_FONT_SANS_14_EM,
      RSD_FONT_SANS_14_NAME }
};

#define FACE_COUNT (sizeof(FACES) / sizeof(FACES[0]))

/* The middle one: DejaVu Sans at a 12-pixel em, which is KDE 4's
 * "Sans Serif 9" at 96 dpi. */
static uint32_t face_at = 1U;

uint32_t rsd_font_size_count(void)
{
    return (uint32_t)FACE_COUNT;
}

uint32_t rsd_font_size_pixels(uint32_t at)
{
    if (at >= FACE_COUNT) {
        return 0U;
    }
    return FACES[at].pixels;
}

bool rsd_font_select(uint32_t at)
{
    if (at >= FACE_COUNT) {
        return false;
    }
    face_at = at;
    return true;
}

uint32_t rsd_font_selected(void)
{
    return face_at;
}

static const struct rsd_glyph *glyph_for(char ch)
{
    uint32_t code = (uint32_t)(unsigned char)ch;
    const struct rsd_font_face *face = &FACES[face_at];

    if (code < face->first || code - face->first >= face->count) {
        return NULL;
    }
    return &face->glyphs[code - face->first];
}

uint32_t rsd_font_ascent(void)
{
    return FACES[face_at].ascent;
}

uint32_t rsd_font_width(const char *text)
{
    uint32_t total = 0U;
    uint32_t at;

    if (text == NULL) {
        return 0U;
    }
    for (at = 0U; text[at] != '\0'; ++at) {
        const struct rsd_glyph *glyph =
            glyph_for(text[at]);

        if (glyph != NULL) {
            total += glyph->advance;
        }
    }
    return total;
}

const char *rsd_font_name(void)
{
    return FACES[face_at].name;
}

uint32_t rsd_font_line_height(void)
{
    return FACES[face_at].height;
}

void rsd_font_draw(struct rsd_surface *surface, struct rsd_rect clip,
    uint32_t x, uint32_t baseline, const char *text, uint32_t colour)
{
    uint32_t pen = x;
    uint32_t at;

    if (text == NULL || baseline < FACES[face_at].ascent) {
        return;
    }
    for (at = 0U; text[at] != '\0'; ++at) {
        const struct rsd_glyph *glyph =
            glyph_for(text[at]);
        uint32_t top = baseline - FACES[face_at].ascent;
        uint32_t row;
        uint32_t column;

        if (glyph == NULL) {
            /* Nothing, rather than a box: a glyph this font does not
             * carry is a gap the caller can see, and a box is a picture
             * of a missing character pretending to be a character. */
            continue;
        }
        for (row = 0U; row < FACES[face_at].height; ++row) {
            for (column = 0U; column < glyph->width; ++column) {
                uint32_t alpha =
                    glyph->coverage[row * glyph->width + column];
                uint32_t under;

                if (alpha == 0U) {
                    continue;
                }
                under = rsd_surface_read(surface, pen + column,
                                           top + row);
                rsd_surface_plot(surface, clip, pen + column, top + row,
                                   rsd_blend(under, colour, alpha));
            }
        }
        pen += glyph->advance;
    }
}

/*
 * Oxygen's caption: the contrast copy one pixel down, then the real one.
 *
 * Quoted from oxygendecoration.cpp:
 *
 *     painter->setPen(contrast);
 *     painter->translate(0, 1);
 *     painter->drawText(...);
 *     painter->translate(0, -1);
 *     painter->setPen(color);
 *     painter->drawText(...);
 *
 * One pixel, downwards, always - not a shadow that grows with the font
 * size and not an outline round the glyphs.
 */
void rsd_font_draw_embossed(struct rsd_surface *surface,
    struct rsd_rect clip, uint32_t x, uint32_t baseline,
    const char *text, uint32_t colour, uint32_t contrast)
{
    rsd_font_draw(surface, clip, x, baseline + 1U, text, contrast);
    rsd_font_draw(surface, clip, x, baseline, text, colour);
}
