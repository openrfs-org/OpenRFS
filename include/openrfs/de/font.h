/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_FONT_H_API
#define OPENRFS_FONT_H_API

#include <stdbool.h>
#include <stdint.h>

#include <openrfs/de/surface.h>

/*
 * Text, as coverage.
 *
 * The glyphs are rasterised ahead of time by tools/make-font.py - there
 * is no font server behind a framebuffer - and drawn by tinting that
 * coverage, so one bitmap serves every colour the shell wants text in.
 */
/* The sizes that were generated.  Settings offers these and no others,
 * because a size nobody rasterised has no glyphs to draw. */
uint32_t openrfs_font_size_count(void);
uint32_t openrfs_font_size_points(uint32_t at);
bool openrfs_font_select(uint32_t at);
uint32_t openrfs_font_selected(void);

uint32_t openrfs_font_width(const char *text);
uint32_t openrfs_font_line_height(void);

/* `baseline` is the text baseline, not the top of the cell, because that
 * is what every metric a font ships is measured from. */
void openrfs_font_draw(struct openrfs_surface *surface, struct openrfs_rect clip,
    uint32_t x, uint32_t baseline, const char *text, uint32_t colour);

#endif /* OPENRFS_FONT_H_API */
