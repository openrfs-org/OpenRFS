/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd_desktop/surface.h>

bool rsd_surface_valid(const struct rsd_surface *surface)
{
    return surface != NULL && surface->pixels != NULL &&
        surface->width != 0U && surface->height != 0U;
}

bool rsd_rect_contains(struct rsd_rect box, uint32_t x, uint32_t y)
{
    return x >= box.x && y >= box.y &&
        x < box.x + box.width && y < box.y + box.height;
}

void rsd_surface_plot(struct rsd_surface *surface,
    struct rsd_rect clip, uint32_t x, uint32_t y, uint32_t colour)
{
    if (!rsd_surface_valid(surface)) {
        return;
    }
    if (!rsd_rect_contains(clip, x, y)) {
        return;
    }
    if (x >= surface->width || y >= surface->height) {
        return;
    }
    surface->pixels[y * surface->width + x] = colour & 0x00FFFFFFU;
}

uint32_t rsd_surface_read(const struct rsd_surface *surface,
    uint32_t x, uint32_t y)
{
    if (!rsd_surface_valid(surface) ||
            x >= surface->width || y >= surface->height) {
        return 0U;
    }
    return surface->pixels[y * surface->width + x] & 0x00FFFFFFU;
}

void rsd_surface_fill(struct rsd_surface *surface,
    struct rsd_rect clip, struct rsd_rect box, uint32_t colour)
{
    uint32_t x;
    uint32_t y;

    for (y = box.y; y < box.y + box.height; ++y) {
        for (x = box.x; x < box.x + box.width; ++x) {
            rsd_surface_plot(surface, clip, x, y, colour);
        }
    }
}

static uint32_t mix(uint32_t under, uint32_t over, uint32_t alpha)
{
    return (over * alpha + under * (255U - alpha) + 127U) / 255U;
}

/*
 * A FILL THAT LETS WHAT IS UNDER IT SHOW THROUGH.
 *
 * There is no compositor here and no alpha channel in the framebuffer,
 * so this is the pseudo-transparency an X terminal has always had: the
 * window is drawn AFTER whatever is behind it, so what is behind it is
 * already in the buffer and can simply be read back and mixed with.
 * It follows that a translucent window shows the windows under it and
 * not a blurred copy of the root, which is exactly what xterm -tr does.
 */
void rsd_surface_wash(struct rsd_surface *surface,
    struct rsd_rect clip, struct rsd_rect box, uint32_t colour,
    uint32_t alpha)
{
    uint32_t x;
    uint32_t y;

    if (alpha >= 255U) {
        rsd_surface_fill(surface, clip, box, colour);
        return;
    }
    for (y = box.y; y < box.y + box.height; ++y) {
        for (x = box.x; x < box.x + box.width; ++x) {
            uint32_t under = rsd_surface_read(surface, x, y);

            rsd_surface_plot(surface, clip, x, y,
                               rsd_blend(under, colour, alpha));
        }
    }
}

uint32_t rsd_blend(uint32_t under, uint32_t over, uint32_t alpha)
{
    uint32_t r = mix((under >> 16) & 0xFFU, (over >> 16) & 0xFFU, alpha);
    uint32_t g = mix((under >> 8) & 0xFFU, (over >> 8) & 0xFFU, alpha);
    uint32_t b = mix(under & 0xFFU, over & 0xFFU, alpha);

    return (r << 16) | (g << 8) | b;
}
