/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_FONT_H_API
#define RSD_FONT_H_API

#include <stdbool.h>
#include <stdint.h>

#include <rsd_desktop/surface.h>

/*
 * Text, as coverage.
 *
 * The glyphs are rasterised ahead of time by tools/make-font.py - there
 * is no font server behind a framebuffer - and drawn by tinting that
 * coverage, so one bitmap serves every colour the shell wants text in.
 */
/* The sizes that were generated.  Settings offers these and no others,
 * because a size nobody rasterised has no glyphs to draw. */
uint32_t rsd_font_size_count(void);
uint32_t rsd_font_size_pixels(uint32_t at);
bool rsd_font_select(uint32_t at);
uint32_t rsd_font_selected(void);

uint32_t rsd_font_width(const char *text);
uint32_t rsd_font_line_height(void);
/* Where the baseline sits inside a line, so a caller with a box to
 * centre text in does not have to know which face is selected. */
uint32_t rsd_font_ascent(void);
/* What the selected face is CALLED, out of the generated header rather
 * than typed in here: a fetch that prints a font name should print the
 * one that was actually rasterised. */
const char *rsd_font_name(void);

/*
 * OXYGEN'S EMBOSS, which is how a KDE 4 window caption is drawn.
 *
 * From oxygendecoration.cpp renderTitleText(): the caption is painted
 * twice - once in a contrast colour one pixel LOWER, then in the real
 * colour on top.  It is not a drop shadow and it is not a bevel; it is
 * one pixel of lift, and it is the difference between text lying on the
 * glass and text standing on it.
 */
void rsd_font_draw_embossed(struct rsd_surface *surface,
    struct rsd_rect clip, uint32_t x, uint32_t baseline,
    const char *text, uint32_t colour, uint32_t contrast);

/* `baseline` is the text baseline, not the top of the cell, because that
 * is what every metric a font ships is measured from. */
void rsd_font_draw(struct rsd_surface *surface, struct rsd_rect clip,
    uint32_t x, uint32_t baseline, const char *text, uint32_t colour);

#endif /* RSD_FONT_H_API */
