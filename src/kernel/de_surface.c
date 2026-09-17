/* SPDX-License-Identifier: GPL-3.0-only */
#include <openrfs/de/surface.h>

bool openrfs_surface_valid(const struct openrfs_surface *surface)
{
    return surface != NULL && surface->pixels != NULL &&
        surface->width != 0U && surface->height != 0U;
}

bool openrfs_rect_contains(struct openrfs_rect box, uint32_t x, uint32_t y)
{
    return x >= box.x && y >= box.y &&
        x < box.x + box.width && y < box.y + box.height;
}

void openrfs_surface_plot(struct openrfs_surface *surface,
    struct openrfs_rect clip, uint32_t x, uint32_t y, uint32_t colour)
{
    if (!openrfs_surface_valid(surface)) {
        return;
    }
    if (!openrfs_rect_contains(clip, x, y)) {
        return;
    }
    if (x >= surface->width || y >= surface->height) {
        return;
    }
    surface->pixels[y * surface->width + x] = colour & 0x00FFFFFFU;
}

uint32_t openrfs_surface_read(const struct openrfs_surface *surface,
    uint32_t x, uint32_t y)
{
    if (!openrfs_surface_valid(surface) ||
            x >= surface->width || y >= surface->height) {
        return 0U;
    }
    return surface->pixels[y * surface->width + x] & 0x00FFFFFFU;
}

void openrfs_surface_fill(struct openrfs_surface *surface,
    struct openrfs_rect clip, struct openrfs_rect box, uint32_t colour)
{
    uint32_t x;
    uint32_t y;

    for (y = box.y; y < box.y + box.height; ++y) {
        for (x = box.x; x < box.x + box.width; ++x) {
            openrfs_surface_plot(surface, clip, x, y, colour);
        }
    }
}

static uint32_t mix(uint32_t under, uint32_t over, uint32_t alpha)
{
    return (over * alpha + under * (255U - alpha) + 127U) / 255U;
}

uint32_t openrfs_blend(uint32_t under, uint32_t over, uint32_t alpha)
{
    uint32_t r = mix((under >> 16) & 0xFFU, (over >> 16) & 0xFFU, alpha);
    uint32_t g = mix((under >> 8) & 0xFFU, (over >> 8) & 0xFFU, alpha);
    uint32_t b = mix(under & 0xFFU, over & 0xFFU, alpha);

    return (r << 16) | (g << 8) | b;
}
