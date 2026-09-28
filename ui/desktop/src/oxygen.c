/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd_desktop/oxygen.h>

#include "rsd_glass.h"
#include "rsd_shadow.h"

/*
 * THE SHADES ARE LOOKED UP NOW, NOT COMPUTED.
 *
 * They used to be this:
 *
 *     #define LIGHT_NUM 118    light = base * 118 / 100
 *     #define DARK_NUM   82
 *
 * which is a stand-in and was wrong in a way you could see. Scaling a
 * light grey by 1.12 clips - 0xED * 118 / 100 is 280, which is 255 - so
 * the top of every window came out pure white and the gradient that was
 * meant to run down it had nowhere to start. A window with a white band
 * at the top and a flat grey body is not a lit sheet; it is two flat
 * greys.
 *
 * KDE does not scale channels. KColorScheme::shade() works in HCY and
 * moves the LUMA by an amount that depends on how light the colour
 * already is, which is exactly why an Oxygen window never blows out.
 * That arithmetic wants pow() and this is freestanding code, so it is
 * done once by tools/make-glass.py - which quotes every formula and
 * names the file it came from - and the results are in
 * src/rsd_glass.h.
 */
struct glass {
    uint32_t base;
    uint32_t top;
    uint32_t bottom;
    uint32_t radial;
    uint32_t light;
    uint32_t dark;
    uint32_t shadow;
};

static const struct glass GLASSES[] = {
    { RSD_GLASS_ACTIVE, RSD_GLASS_ACTIVE_TOP, RSD_GLASS_ACTIVE_BOTTOM,
      RSD_GLASS_ACTIVE_RADIAL, RSD_GLASS_ACTIVE_LIGHT,
      RSD_GLASS_ACTIVE_DARK, RSD_GLASS_ACTIVE_SHADOW },
    { RSD_GLASS_IDLE, RSD_GLASS_IDLE_TOP, RSD_GLASS_IDLE_BOTTOM,
      RSD_GLASS_IDLE_RADIAL, RSD_GLASS_IDLE_LIGHT,
      RSD_GLASS_IDLE_DARK, RSD_GLASS_IDLE_SHADOW }
};

#define GLASS_COUNT (sizeof(GLASSES) / sizeof(GLASSES[0]))

/* The fallback, for a base nobody derived. It is the old scaling, and it
 * is here so a theme that has not been through tools/make-glass.py still
 * draws rather than drawing nothing - not because it is right. */
#define LIGHT_NUM  118U
#define DARK_NUM    82U
#define SHADOW_NUM  46U
#define SHADE_DEN  100U

static uint32_t channel(uint32_t colour, uint32_t shift)
{
    return (colour >> shift) & 0xFFU;
}

static uint32_t pack(uint32_t r, uint32_t g, uint32_t b)
{
    return (r << 16) | (g << 8) | b;
}

static uint32_t clamp(uint32_t v)
{
    return v > 255U ? 255U : v;
}

uint32_t rsd_oxy_mix(uint32_t a, uint32_t b, uint32_t t)
{
    uint32_t out[3];
    uint32_t i;

    if (t > 255U) {
        t = 255U;
    }
    for (i = 0U; i < 3U; ++i) {
        uint32_t shift = i * 8U;
        uint32_t from = channel(a, shift);
        uint32_t to = channel(b, shift);

        /* Signed subtraction avoided: the two directions are separate,
         * which keeps everything unsigned on a machine with no libgcc. */
        out[i] = to >= from ? from + (to - from) * t / 255U
                            : from - (from - to) * t / 255U;
    }
    return pack(out[2], out[1], out[0]);
}

uint32_t rsd_oxy_shade(uint32_t colour, uint32_t num, uint32_t den)
{
    if (den == 0U) {
        return colour;
    }
    return pack(clamp(channel(colour, 16U) * num / den),
                clamp(channel(colour, 8U) * num / den),
                clamp(channel(colour, 0U) * num / den));
}

static const struct glass *glass_for(uint32_t base)
{
    uint32_t at;

    for (at = 0U; at < GLASS_COUNT; ++at) {
        if (GLASSES[at].base == base) {
            return &GLASSES[at];
        }
    }
    return NULL;
}

uint32_t rsd_oxy_light(uint32_t colour)
{
    const struct glass *glass = glass_for(colour);

    return glass != NULL ? glass->light
        : rsd_oxy_shade(colour, LIGHT_NUM, SHADE_DEN);
}

uint32_t rsd_oxy_dark(uint32_t colour)
{
    const struct glass *glass = glass_for(colour);

    return glass != NULL ? glass->dark
        : rsd_oxy_shade(colour, DARK_NUM, SHADE_DEN);
}

uint32_t rsd_oxy_shadow(uint32_t colour)
{
    const struct glass *glass = glass_for(colour);

    return glass != NULL ? glass->shadow
        : rsd_oxy_shade(colour, SHADOW_NUM, SHADE_DEN);
}

/*
 * THREE STOPS, NOT TWO, and the middle one is the base colour itself.
 *
 * From Helper::verticalGradient():
 *
 *     gradient.setColorAt(0.0, backgroundTopColor(color));
 *     gradient.setColorAt(0.5, color);
 *     gradient.setColorAt(1.0, backgroundBottomColor(color));
 *
 * Two stops - light at the top, dark at the bottom - is a ramp, and a
 * ramp reads as a ramp. Three, with the window's own colour halfway
 * down, is what makes the top half look lit and the bottom half look
 * shaded rather than the whole thing look tilted.
 */
uint32_t rsd_oxy_window(uint32_t base, uint32_t y, uint32_t win_y,
    uint32_t win_h)
{
    const struct glass *glass = glass_for(base);
    uint32_t top = glass != NULL ? glass->top
        : rsd_oxy_shade(base, 112U, SHADE_DEN);
    uint32_t bottom = glass != NULL ? glass->bottom
        : rsd_oxy_shade(base, 94U, SHADE_DEN);
    uint32_t split = win_h * 3U / 4U;
    uint32_t dy;

    if (split > RSD_GLASS_SPLIT) {
        split = RSD_GLASS_SPLIT;
    }
    if (split == 0U || y <= win_y) {
        return top;
    }
    dy = y - win_y;
    if (dy >= split) {
        return bottom;
    }
    if (dy * 2U < split) {
        return rsd_oxy_mix(top, base, dy * 2U * 255U / split);
    }
    return rsd_oxy_mix(base, bottom, (dy * 2U - split) * 255U / split);
}

/*
 * THE POOL OF LIGHT ON THE TOP EDGE.
 *
 * The other half of renderWindowBackground(), and the half everybody
 * leaves out: a radial highlight centred on the middle of the window's
 * top edge, at most 600 pixels wide and 64 tall, laid over the vertical
 * gradient. It is the reason a wide KDE 4 window is brighter in the
 * middle of its title bar than at the ends, and without it a window is
 * evenly lit - which nothing in a room ever is.
 *
 * Elliptical, because Qt draws it into a 128-unit-wide space stretched
 * across the width: 300 pixels of horizontal radius against 64 of
 * vertical.
 */
uint32_t rsd_oxy_pool(uint32_t base, uint32_t under, uint32_t x,
    uint32_t y, struct rsd_rect window)
{
    const struct glass *glass = glass_for(base);
    uint32_t wide = window.width < RSD_GLASS_POOL_WIDE ?
        window.width : RSD_GLASS_POOL_WIDE;
    uint32_t half = wide / 2U;
    uint32_t centre = window.x + window.width / 2U;
    uint32_t dx = x > centre ? x - centre : centre - x;
    uint32_t dy = y > window.y ? y - window.y : 0U;
    uint32_t squared;
    uint32_t alpha;

    if (glass == NULL || half == 0U || dx >= half
            || dy >= RSD_GLASS_POOL_HIGH) {
        return under;
    }
    /* The squared normalised radius, scaled to 0..255 so it indexes the
     * table straight. No root anywhere: the stops were interpolated
     * against the root of this when the table was generated. */
    squared = dx * dx * 256U / (half * half)
        + dy * dy * 256U / (RSD_GLASS_POOL_HIGH * RSD_GLASS_POOL_HIGH);
    if (squared > 255U) {
        return under;
    }
    alpha = rsd_glass_pool[squared];
    if (alpha == 0U) {
        return under;
    }
    return rsd_oxy_mix(under, glass->radial, alpha);
}

uint32_t rsd_oxy_slab(uint32_t base, uint32_t y, uint32_t top,
    uint32_t height, bool sunken)
{
    uint32_t light = rsd_oxy_light(base);
    uint32_t span;
    uint32_t start;
    uint32_t pos;

    if (height == 0U) {
        return base;
    }
    if (sunken) {
        /* from top to bottom + h: twice the button's height, base at
         * the top and light at the far bottom. */
        span = height * 2U;
        start = top;
        if (y <= start) {
            return base;
        }
        pos = (y - start) * 255U / span;
        return rsd_oxy_mix(base, light, pos > 255U ? 255U : pos);
    }

    /* from top - 0.2h to bottom + 0.4h, which is 1.6h of gradient for
     * a button h tall. The visible part is the middle of it. */
    span = height * 16U / 10U;
    start = top > height * 2U / 10U ? top - height * 2U / 10U : 0U;
    if (y <= start) {
        return light;
    }
    pos = (y - start) * 255U / span;
    if (pos >= 153U) {          /* stop 0.6 of the span */
        return base;
    }
    return rsd_oxy_mix(light, base, pos * 255U / 153U);
}

bool rsd_oxy_outside(struct rsd_rect box, uint32_t x, uint32_t y,
    uint32_t radius, bool top_only)
{
    uint32_t dx;
    uint32_t dy;

    if (radius == 0U || box.width < radius * 2U || box.height < radius * 2U) {
        return false;
    }
    if (x < box.x + radius) {
        dx = box.x + radius - x;
    } else if (x + radius >= box.x + box.width) {
        dx = x + radius + 1U - (box.x + box.width);
    } else {
        return false;
    }
    if (y < box.y + radius) {
        dy = box.y + radius - y;
    } else if (!top_only && y + radius >= box.y + box.height) {
        dy = y + radius + 1U - (box.y + box.height);
    } else {
        return false;
    }
    /* Squared distance, so there is no root and no float. */
    return dx * dx + dy * dy > radius * radius;
}

void rsd_oxy_sheet(struct rsd_surface *surface, struct rsd_rect clip,
    struct rsd_rect box, uint32_t base, uint32_t radius, bool top_only)
{
    uint32_t edge = rsd_oxy_shadow(base);
    uint32_t inner = rsd_oxy_light(base);
    uint32_t x;
    uint32_t y;

    if (!rsd_surface_valid(surface)) {
        return;
    }
    for (y = box.y; y < box.y + box.height; ++y) {
        uint32_t row = rsd_oxy_window(base, y, box.y, box.height);

        for (x = box.x; x < box.x + box.width; ++x) {
            uint32_t colour = row;

            if (rsd_oxy_outside(box, x, y, radius, top_only)) {
                continue;
            }
            /* One dark line outside, one light line just within it.
             * That pair is the whole of Oxygen's relief: there is no
             * bevel, no second ring and no corner handles. */
            if (rsd_oxy_outside(box, x, y, radius ? radius - 1U : 0U,
                                  top_only)
                || x == box.x || y == box.y
                || x + 1U == box.x + box.width
                || y + 1U == box.y + box.height) {
                colour = edge;
            } else if (x == box.x + 1U || y == box.y + 1U
                       || x + 2U == box.x + box.width
                       || y + 2U == box.y + box.height) {
                colour = rsd_oxy_mix(row, inner, 150U);
            }
            rsd_surface_plot(surface, clip, x, y, colour);
        }
    }
}

/*
 * A BUTTON, which is a slab with a rim and nothing else.
 *
 * Three places drew one before this existed and all three drew the same
 * thing: a flat fill with a light line along the top and left and a dark
 * one along the bottom and right. That is a bevel, it is what a button
 * looked like in 1995, and it says so from across the room.
 *
 * Oxygen has no bevel. The shape comes from the light falling across the
 * face - see rsd_oxy_slab(), whose gradient is taller than the button
 * so what you see is a slice out of the middle of it - and the only line
 * is the dark one holding the edge, with the corners rounded off it.
 */
void rsd_oxy_button(struct rsd_surface *surface, struct rsd_rect clip,
    struct rsd_rect box, uint32_t base, bool sunken)
{
    uint32_t edge = rsd_oxy_dark(base);
    uint32_t x;
    uint32_t y;

    if (!rsd_surface_valid(surface) || box.height == 0U) {
        return;
    }
    for (y = box.y; y < box.y + box.height; ++y) {
        uint32_t row = rsd_oxy_slab(base, y, box.y, box.height, sunken);

        for (x = box.x; x < box.x + box.width; ++x) {
            if (rsd_oxy_outside(box, x, y, RSD_OXY_BUTTON_RADIUS,
                                  false)) {
                continue;
            }
            rsd_surface_plot(surface, clip, x, y,
                rsd_oxy_outside(box, x, y, RSD_OXY_BUTTON_RADIUS - 1U,
                                  false) ? edge : row);
        }
    }
}

void rsd_oxy_blit(struct rsd_surface *surface, struct rsd_rect clip,
    uint32_t left, uint32_t top, const uint32_t *argb, uint32_t side)
{
    uint32_t y;

    if (!rsd_surface_valid(surface) || argb == NULL) {
        return;
    }
    for (y = 0U; y < side; ++y) {
        uint32_t x;

        for (x = 0U; x < side; ++x) {
            uint32_t cell = argb[y * side + x];
            uint32_t alpha = (cell >> 24) & 0xFFU;

            if (alpha == 0U) {
                continue;
            }
            rsd_surface_plot(surface, clip, left + x, top + y,
                alpha == 255U ? (cell & 0xFFFFFFU)
                              : rsd_oxy_mix(
                                    rsd_surface_read(surface, left + x,
                                                       top + y),
                                    cell & 0xFFFFFFU, alpha));
        }
    }
}

/*
 * THE SHADOW, AND THE ACTIVE WINDOW'S IS A BLUE GLOW.
 *
 * This is the thing that was missing. A KDE 4 window - or menu, or anything
 * else that floats - does not sit on the
 * wallpaper, it floats over it, and the FOCUSED one is haloed in cyan -
 * Oxygen's ActiveShadow is InnerColor 112,239,255 over OuterColor
 * 84,167,240, both at forty pixels. Nothing else on this screen says the
 * year as loudly, and no amount of gradient in the frame substitutes for
 * it: without the halo the windows are stickers.
 *
 * src/rsd_shadow.h holds the falloff as a table indexed by the SQUARED
 * distance from the window's edge, which is what lets a radial gradient
 * be drawn round a rectangle with no square root: along a side the
 * distance is the perpendicular one, at a corner it is sqrt(dx^2 + dy^2),
 * and dx^2 + dy^2 is what the loop already has.
 *
 * THE BOX ITSELF IS SKIPPED, not shadowed and then painted over. A
 * sheer terminal reads the framebuffer back through its own client area,
 * and a shadow laid under the whole window would be the first thing it
 * found.
 */
void rsd_oxy_cast(struct rsd_surface *surface,
    struct rsd_rect box, bool active)
{
    const uint32_t *table = active ? rsd_shadow_active
                                           : rsd_shadow_idle;
    struct rsd_rect cast = box;
    struct rsd_rect band;
    uint32_t x;
    uint32_t y;

    /* The inactive shadow hangs a little below the window, the active one
     * does not: Oxygen's VerticalOffset is 0.2 for one and 0 for the
     * other. Moving the rect the shadow is cast from is the whole of it. */
    if (!active) {
        cast.y += RSD_SHADOW_VOFFSET;
    }
    band.x = box.x > RSD_SHADOW_SIZE ?
        box.x - RSD_SHADOW_SIZE : 0U;
    band.y = box.y > RSD_SHADOW_SIZE ?
        box.y - RSD_SHADOW_SIZE : 0U;
    band.width = box.x + box.width
        + RSD_SHADOW_SIZE - band.x;
    band.height = box.y + box.height
        + RSD_SHADOW_SIZE + RSD_SHADOW_VOFFSET - band.y;

    for (y = band.y; y < band.y + band.height; ++y) {
        for (x = band.x; x < band.x + band.width; ++x) {
            uint32_t dx = 0U;
            uint32_t dy = 0U;
            uint32_t squared;
            uint32_t cell;
            uint32_t alpha;

            if (rsd_rect_contains(box, x, y)) {
                continue;
            }
            if (x < cast.x) {
                dx = cast.x - x;
            } else if (x >= cast.x + cast.width) {
                dx = x - (cast.x + cast.width) + 1U;
            }
            if (y < cast.y) {
                dy = cast.y - y;
            } else if (y >= cast.y + cast.height) {
                dy = y - (cast.y + cast.height) + 1U;
            }
            squared = dx * dx + dy * dy;
            if (squared >= RSD_SHADOW_SPAN) {
                continue;
            }
            cell = table[squared];
            alpha = (cell >> 24) & 0xFFU;
            if (alpha == 0U) {
                continue;
            }
            rsd_surface_plot(surface, band, x, y,
                rsd_blend(rsd_surface_read(surface, x, y),
                            cell & 0xFFFFFFU, alpha));
        }
    }
}
