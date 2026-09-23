/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/font.h>

#include "trait_font_6x13.h"
#include "trait_font_7x14.h"
#include "trait_font_9x18.h"

/*
 * THREE SIZES, UNPACKED AHEAD OF TIME.
 *
 * There is no font server behind a framebuffer and no scaler either, so
 * "a different font size" means a different set of bitmaps.  Settings
 * offers the three that exist; offering a slider over sizes that were
 * never generated would be a control that does nothing, and scaling one
 * set to stand in for the others is how text stops looking like text.
 *
 * The three are Misc-Fixed 6x13, 7x14 and 9x18 - the sizes an X session
 * has offered since R6, and the ones people actually name in .Xdefaults.
 * They are measured in pixels because that is the only unit a bitmap
 * font has; a point size would be a number with nothing behind it.
 */
struct trait_font_face {
    const struct trait_glyph *glyphs;
    uint32_t count;
    uint32_t first;
    uint32_t ascent;
    uint32_t height;
    uint32_t pixels;
};

static const struct trait_font_face FACES[] = {
    { (const struct trait_glyph *)trait_font_6x13,
      sizeof(trait_font_6x13) / sizeof(trait_font_6x13[0]),
      TRAIT_FONT_6X13_FIRST, TRAIT_FONT_6X13_ASCENT, TRAIT_FONT_6X13_HEIGHT,
      13U },
    { (const struct trait_glyph *)trait_font_7x14,
      sizeof(trait_font_7x14) / sizeof(trait_font_7x14[0]),
      TRAIT_FONT_7X14_FIRST, TRAIT_FONT_7X14_ASCENT, TRAIT_FONT_7X14_HEIGHT,
      14U },
    { (const struct trait_glyph *)trait_font_9x18,
      sizeof(trait_font_9x18) / sizeof(trait_font_9x18[0]),
      TRAIT_FONT_9X18_FIRST, TRAIT_FONT_9X18_ASCENT, TRAIT_FONT_9X18_HEIGHT,
      18U }
};

#define FACE_COUNT (sizeof(FACES) / sizeof(FACES[0]))

static uint32_t face_at = 1U;   /* 7x14 */

uint32_t trait_font_size_count(void)
{
    return (uint32_t)FACE_COUNT;
}

uint32_t trait_font_size_pixels(uint32_t at)
{
    if (at >= FACE_COUNT) {
        return 0U;
    }
    return FACES[at].pixels;
}

bool trait_font_select(uint32_t at)
{
    if (at >= FACE_COUNT) {
        return false;
    }
    face_at = at;
    return true;
}

uint32_t trait_font_selected(void)
{
    return face_at;
}

static const struct trait_glyph *glyph_for(char ch)
{
    uint32_t code = (uint32_t)(unsigned char)ch;
    const struct trait_font_face *face = &FACES[face_at];

    if (code < face->first || code - face->first >= face->count) {
        return NULL;
    }
    return &face->glyphs[code - face->first];
}

uint32_t trait_font_width(const char *text)
{
    uint32_t total = 0U;
    uint32_t at;

    if (text == NULL) {
        return 0U;
    }
    for (at = 0U; text[at] != '\0'; ++at) {
        const struct trait_glyph *glyph =
            glyph_for(text[at]);

        if (glyph != NULL) {
            total += glyph->advance;
        }
    }
    return total;
}

uint32_t trait_font_line_height(void)
{
    return FACES[face_at].height;
}

void trait_font_draw(struct trait_surface *surface, struct trait_rect clip,
    uint32_t x, uint32_t baseline, const char *text, uint32_t colour)
{
    uint32_t pen = x;
    uint32_t at;

    if (text == NULL || baseline < FACES[face_at].ascent) {
        return;
    }
    for (at = 0U; text[at] != '\0'; ++at) {
        const struct trait_glyph *glyph =
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
                under = trait_surface_read(surface, pen + column,
                                           top + row);
                trait_surface_plot(surface, clip, pen + column, top + row,
                                   trait_blend(under, colour, alpha));
            }
        }
        pen += glyph->advance;
    }
}
