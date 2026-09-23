/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/gears.h>

#include "trait_gears_art.h"

/*
 * A GEAR IS ONE OUTLINE SWEPT ALONG ONE VECTOR.
 *
 * Projected orthographically, an extruded flat shape is its own outline
 * drawn once per plane between the back face and the front, and because
 * the projection of a constant-z plane is affine, every one of those is
 * the same outline translated.  So the solid is drawn back to front, one
 * pass per pixel of travel, and the nearer pass paints over the farther
 * one for free.  The last pass is the face the light reaches and is
 * painted in the brighter of the gear's two colours; everything still
 * showing behind it is a side wall.
 *
 * The bore is filled in the same pass under the even-odd rule rather
 * than punched out afterwards.  Punching it afterwards would take the
 * inner wall with it - the far side of the hole, which you can see
 * through the near side, and which is the detail that makes the hole
 * read as a hole rather than as a gap.
 *
 * Coordinates are 1/256 of a pixel from here down.  Whole pixels put
 * the teeth on a ladder: twenty of them around a circle land on
 * angles that do not divide into pixels, and rounding each vertex makes
 * some teeth a pixel fatter than their neighbours.
 */
#define SUB 256                 /* sub-pixel units per pixel */
#define CROSSINGS 192U          /* > the most edges any outline has */

/* Three fifths of the shorter side. Larger and the teeth run off the
 * screen on a 4:3 display; smaller and the root looks like a window. */
#define FILL_NUMERATOR 3U
#define FILL_DENOMINATOR 5U

static int32_t place(int32_t view, int32_t scale, int32_t origin)
{
    return origin + (view * scale) / (int32_t)TRAIT_GEARS_UNIT;
}

/*
 * Where this contour crosses the scanline, appended to `out`.
 *
 * `at` is how many are already there: one gear's outline and its bore
 * are two contours of one even-odd fill, so they share a list.
 */
static uint32_t cross(const int32_t *points, uint32_t count,
                      int32_t scale, int32_t ox, int32_t oy,
                      int32_t middle, int32_t *out, uint32_t at)
{
    uint32_t i;

    for (i = 0U; i < count; ++i) {
        uint32_t j = (i + 1U) % count;
        int32_t ax = place(points[i * 2U], scale, ox);
        int32_t ay = place(points[i * 2U + 1U], scale, oy);
        int32_t bx = place(points[j * 2U], scale, ox);
        int32_t by = place(points[j * 2U + 1U], scale, oy);
        int32_t along;
        int32_t x;

        /* Half-open in y, so a vertex exactly on the scanline is counted
         * once rather than twice or not at all. */
        if ((ay <= middle) == (by <= middle)) {
            continue;
        }
        if (at >= CROSSINGS) {
            break;
        }
        /* How far along the edge the scanline falls, in 1/4096.  The
         * obvious (bx - ax) * (middle - ay) leaves 32 bits on a large
         * screen, and widening it to 64 would put a libgcc division in
         * a kernel that links no libraries at all. */
        along = ((middle - ay) * 4096) / (by - ay);
        x = ax + ((bx - ax) * along) / 4096;
        out[at] = x;
        ++at;
    }
    return at;
}

static void sort(int32_t *values, uint32_t count)
{
    uint32_t i;

    for (i = 1U; i < count; ++i) {
        int32_t key = values[i];
        uint32_t j = i;

        while (j > 0U && values[j - 1U] > key) {
            values[j] = values[j - 1U];
            --j;
        }
        values[j] = key;
    }
}

static void span(struct trait_surface *surface, struct trait_rect clip,
                 int32_t left, int32_t right, uint32_t y, uint32_t colour)
{
    struct trait_rect box = { 0U, 0U, 0U, 0U };
    int32_t first = (left + SUB / 2) / SUB;
    int32_t last = (right + SUB / 2) / SUB;

    if (last <= first || last <= 0) {
        return;
    }
    if (first < 0) {
        first = 0;
    }
    box.x = (uint32_t)first;
    box.y = y;
    box.width = (uint32_t)(last - first);
    box.height = 1U;
    trait_surface_fill(surface, clip, box, colour);
}

static void shape(struct trait_surface *surface, struct trait_rect clip,
                  const struct trait_gears_shape *gear, int32_t scale,
                  int32_t ox, int32_t oy, uint32_t colour)
{
    uint32_t y;

    for (y = clip.y; y < clip.y + clip.height; ++y) {
        int32_t hits[CROSSINGS];
        int32_t middle = (int32_t)y * SUB + SUB / 2;
        uint32_t count = cross(gear->outline, gear->outline_points,
                               scale, ox, oy, middle, hits, 0U);
        uint32_t i;

        count = cross(gear->bore, gear->bore_points,
                      scale, ox, oy, middle, hits, count);
        if (count < 2U) {
            continue;
        }
        sort(hits, count);
        for (i = 0U; i + 1U < count; i += 2U) {
            span(surface, clip, hits[i], hits[i + 1U], y, colour);
        }
    }
}

void trait_gears_draw(struct trait_surface *surface, struct trait_rect clip)
{
    int32_t wide = TRAIT_GEARS_MAX_X - TRAIT_GEARS_MIN_X;
    int32_t tall = TRAIT_GEARS_MAX_Y - TRAIT_GEARS_MIN_Y;
    int32_t across;
    int32_t down;
    int32_t scale;
    int32_t ox;
    int32_t oy;
    uint32_t at;

    if (!trait_surface_valid(surface) || clip.width == 0U ||
        clip.height == 0U || wide <= 0 || tall <= 0) {
        return;
    }
    trait_surface_fill(surface, clip, clip, 0x000000U);

    /* Sub-pixels per view unit, taking the tighter of the two fits.  The
     * fraction is taken off the screen rather than off the result so
     * that a wide display cannot push the multiplication past 32 bits;
     * it holds to a little over four thousand pixels across. */
    across = (int32_t)(clip.width * FILL_NUMERATOR / FILL_DENOMINATOR);
    down = (int32_t)(clip.height * FILL_NUMERATOR / FILL_DENOMINATOR);
    if (across > 4096) {
        across = 4096;
    }
    if (down > 4096) {
        down = 4096;
    }
    scale = (across * SUB * (int32_t)TRAIT_GEARS_UNIT) / wide;
    if ((down * SUB * (int32_t)TRAIT_GEARS_UNIT) / tall < scale) {
        scale = (down * SUB * (int32_t)TRAIT_GEARS_UNIT) / tall;
    }
    ox = (int32_t)(clip.x * SUB) + (int32_t)(clip.width * SUB) / 2
        - place((TRAIT_GEARS_MIN_X + TRAIT_GEARS_MAX_X) / 2, scale, 0);
    oy = (int32_t)(clip.y * SUB) + (int32_t)(clip.height * SUB) / 2
        - place((TRAIT_GEARS_MIN_Y + TRAIT_GEARS_MAX_Y) / 2, scale, 0);

    for (at = 0U; at < TRAIT_GEARS_COUNT; ++at) {
        const struct trait_gears_shape *gear = &trait_gears[at];
        int32_t sx = (gear->sweep_x * scale) / (int32_t)TRAIT_GEARS_UNIT;
        int32_t sy = (gear->sweep_y * scale) / (int32_t)TRAIT_GEARS_UNIT;
        int32_t reach = (sx < 0 ? -sx : sx) + (sy < 0 ? -sy : sy);
        int32_t steps = reach / SUB;
        int32_t step;

        if (steps < 1) {
            steps = 1;
        }
        for (step = 0; step <= steps; ++step) {
            shape(surface, clip, gear, scale,
                  ox + (sx * step) / steps, oy + (sy * step) / steps,
                  step == steps ? gear->face : gear->side);
        }
    }
}
