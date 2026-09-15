/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_DE_SURFACE_H
#define TRAIT_DE_SURFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A plain 32-bit surface, 0x00RRGGBB per pixel.
 *
 * THIS IS THE WHOLE PLATFORM.  Trait OS in C targets a linear
 * framebuffer and nothing else: no allocator, no toolkit, no font
 * server.  Everything above this file draws by writing pixels into one
 * of these, so the same code serves a real framebuffer and the host
 * harness that writes PNGs, and neither is compiled into the other.
 */
struct trait_surface {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
};

struct trait_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

bool trait_surface_valid(const struct trait_surface *surface);
bool trait_rect_contains(struct trait_rect box, uint32_t x, uint32_t y);

/* Clipped: a write outside `clip` or outside the surface is dropped
 * rather than wrapping onto the next row, which is the bug this exists
 * to make impossible. */
void trait_surface_plot(struct trait_surface *surface,
    struct trait_rect clip, uint32_t x, uint32_t y, uint32_t colour);
uint32_t trait_surface_read(const struct trait_surface *surface,
    uint32_t x, uint32_t y);
void trait_surface_fill(struct trait_surface *surface,
    struct trait_rect clip, struct trait_rect box, uint32_t colour);

/* Straight (unpremultiplied) source-over, which is what the icon planes
 * carry and what a framebuffer without an alpha channel needs. */
uint32_t trait_blend(uint32_t under, uint32_t over, uint32_t alpha);

#endif /* TRAIT_DE_SURFACE_H */
