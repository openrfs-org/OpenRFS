/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_DESKTOP_SURFACE_H
#define RSD_DESKTOP_SURFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A plain 32-bit surface, 0x00RRGGBB per pixel.
 *
 * THIS IS THE WHOLE PLATFORM.  RSD OS in C targets a linear
 * framebuffer and nothing else: no allocator, no toolkit, no font
 * server.  Everything above this file draws by writing pixels into one
 * of these, so the same code serves a real framebuffer and the host
 * harness that writes PNGs, and neither is compiled into the other.
 */
struct rsd_surface {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
};

struct rsd_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

bool rsd_surface_valid(const struct rsd_surface *surface);
bool rsd_rect_contains(struct rsd_rect box, uint32_t x, uint32_t y);

/* Clipped: a write outside `clip` or outside the surface is dropped
 * rather than wrapping onto the next row, which is the bug this exists
 * to make impossible. */
void rsd_surface_plot(struct rsd_surface *surface,
    struct rsd_rect clip, uint32_t x, uint32_t y, uint32_t colour);
uint32_t rsd_surface_read(const struct rsd_surface *surface,
    uint32_t x, uint32_t y);
void rsd_surface_fill(struct rsd_surface *surface,
    struct rsd_rect clip, struct rsd_rect box, uint32_t colour);

/* A fill that reads the surface back and mixes with it, which is the
 * pseudo-transparency an X terminal has always had: no compositor, no
 * alpha channel, just drawing after whatever is behind you. */
void rsd_surface_wash(struct rsd_surface *surface,
    struct rsd_rect clip, struct rsd_rect box, uint32_t colour,
    uint32_t alpha);

/* Straight (unpremultiplied) source-over, which is what the icon planes
 * carry and what a framebuffer without an alpha channel needs. */
uint32_t rsd_blend(uint32_t under, uint32_t over, uint32_t alpha);

#endif /* RSD_DESKTOP_SURFACE_H */
