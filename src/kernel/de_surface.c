/* SPDX-License-Identifier: GPL-3.0-only */
#include <opengat/de/surface.h>

bool opengat_surface_valid(const struct opengat_surface *surface)
{
    return surface != NULL && surface->pixels != NULL &&
        surface->width != 0U && surface->height != 0U;
}

bool opengat_rect_contains(struct opengat_rect box, uint32_t x, uint32_t y)
{
    return x >= box.x && y >= box.y &&
        x < box.x + box.width && y < box.y + box.height;
}

void opengat_surface_plot(struct opengat_surface *surface,
    struct opengat_rect clip, uint32_t x, uint32_t y, uint32_t colour)
{
    if (!opengat_surface_valid(surface)) {
        return;
    }
    if (!opengat_rect_contains(clip, x, y)) {
        return;
    }
    if (x >= surface->width || y >= surface->height) {
        return;
    }
    surface->pixels[y * surface->width + x] = colour & 0x00FFFFFFU;
}

uint32_t opengat_surface_read(const struct opengat_surface *surface,
    uint32_t x, uint32_t y)
{
    if (!opengat_surface_valid(surface) ||
            x >= surface->width || y >= surface->height) {
        return 0U;
    }
    return surface->pixels[y * surface->width + x] & 0x00FFFFFFU;
}

void opengat_surface_fill(struct opengat_surface *surface,
    struct opengat_rect clip, struct opengat_rect box, uint32_t colour)
{
    uint32_t x;
    uint32_t y;

    for (y = box.y; y < box.y + box.height; ++y) {
        for (x = box.x; x < box.x + box.width; ++x) {
            opengat_surface_plot(surface, clip, x, y, colour);
        }
    }
}

static uint32_t mix(uint32_t under, uint32_t over, uint32_t alpha)
{
    return (over * alpha + under * (255U - alpha) + 127U) / 255U;
}

uint32_t opengat_blend(uint32_t under, uint32_t over, uint32_t alpha)
{
    uint32_t r = mix((under >> 16) & 0xFFU, (over >> 16) & 0xFFU, alpha);
    uint32_t g = mix((under >> 8) & 0xFFU, (over >> 8) & 0xFFU, alpha);
    uint32_t b = mix(under & 0xFFU, over & 0xFFU, alpha);

    return (r << 16) | (g << 8) | b;
}
