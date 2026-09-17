/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DE_SURFACE_H
#define OPENRFS_DE_SURFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A plain 32-bit surface, 0x00RRGGBB per pixel.
 *
 * THIS IS THE WHOLE PLATFORM.  OpenRFS in C targets a linear
 * framebuffer and nothing else: no allocator, no toolkit, no font
 * server.  Everything above this file draws by writing pixels into one
 * of these, so the same code serves a real framebuffer and the host
 * harness that writes PNGs, and neither is compiled into the other.
 */
struct openrfs_surface {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
};

struct openrfs_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

bool openrfs_surface_valid(const struct openrfs_surface *surface);
bool openrfs_rect_contains(struct openrfs_rect box, uint32_t x, uint32_t y);

/* Clipped: a write outside `clip` or outside the surface is dropped
 * rather than wrapping onto the next row, which is the bug this exists
 * to make impossible. */
void openrfs_surface_plot(struct openrfs_surface *surface,
    struct openrfs_rect clip, uint32_t x, uint32_t y, uint32_t colour);
uint32_t openrfs_surface_read(const struct openrfs_surface *surface,
    uint32_t x, uint32_t y);
void openrfs_surface_fill(struct openrfs_surface *surface,
    struct openrfs_rect clip, struct openrfs_rect box, uint32_t colour);

/* Straight (unpremultiplied) source-over, which is what the icon planes
 * carry and what a framebuffer without an alpha channel needs. */
uint32_t openrfs_blend(uint32_t under, uint32_t over, uint32_t alpha);

#endif /* OPENRFS_DE_SURFACE_H */
