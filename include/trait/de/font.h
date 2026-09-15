/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_FONT_H_API
#define TRAIT_FONT_H_API

#include <stdbool.h>
#include <stdint.h>

#include <trait/de/surface.h>

/*
 * Text, as coverage.
 *
 * The glyphs are rasterised ahead of time by tools/make-font.py - there
 * is no font server behind a framebuffer - and drawn by tinting that
 * coverage, so one bitmap serves every colour the shell wants text in.
 */
/* The sizes that were generated.  Settings offers these and no others,
 * because a size nobody rasterised has no glyphs to draw. */
uint32_t trait_font_size_count(void);
uint32_t trait_font_size_points(uint32_t at);
bool trait_font_select(uint32_t at);
uint32_t trait_font_selected(void);

uint32_t trait_font_width(const char *text);
uint32_t trait_font_line_height(void);

/* `baseline` is the text baseline, not the top of the cell, because that
 * is what every metric a font ships is measured from. */
void trait_font_draw(struct trait_surface *surface, struct trait_rect clip,
    uint32_t x, uint32_t baseline, const char *text, uint32_t colour);

#endif /* TRAIT_FONT_H_API */
