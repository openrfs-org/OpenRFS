/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd_desktop/panel.h>

#include <rsd_desktop/font.h>
#include <rsd_desktop/oxygen.h>
#include <rsd_desktop/theme.h>

#include "rsd_icons.h"
#include "rsd_logo.h"

/*
 * THE PANEL.
 *
 * A bar along the bottom with a launcher at one end, the open windows
 * in the middle, a tray and a clock at the other. That arrangement is
 * not a choice - it is what every desktop of the period settled on, and
 * the muscle memory that goes with it is the reason to keep it.
 *
 * THE GLASS IS READ BACK, NOT FILLED. There is no compositor here and
 * no alpha channel on the framebuffer, so the bar mixes with whatever
 * is already under it - the same pseudo-transparency an X terminal has
 * always had, and the same call the sheer terminal uses. What is behind
 * it is the wallpaper, so the bar is genuinely the colour of what it is
 * over: dark where the sky is, blue where the limb runs under it. A
 * flat fill picked to look about right would be a bar that never
 * changes, and over a picture with a bright arc across the bottom there
 * is no one colour that is right along the whole width.
 *
 * The first cut washed the whole bar at one alpha and came out a thin
 * flat stripe. A panel of this era is not one tint: it is darkest at
 * its top edge, eases through the middle and firms up again at the
 * bottom, with exactly one hard highlight along the top. That is what
 * separates it from the picture behind it without drawing a border.
 */

/* The glass, and how much of what is behind it survives at the top of
 * the bar, in the middle, and at the bottom. */
#define GLASS      0x070B14U
#define SHEER_TOP    212U
#define SHEER_MID    170U
#define SHEER_FOOT   200U

/* The one line along the top edge, and it is a WASH rather than a
 * colour: a hard light-blue rule reads as a border drawn round the bar,
 * and what a panel of this period has is a lit edge - the same glass,
 * catching the light. */
#define EDGE       0xC8D8F0U
#define EDGE_ALPHA   150U
#define EDGE_UNDER    70U

#define INK        0xF0F3FAU
#define INK_DIM    0x9AA4BAU

#define PAD 4U

/* What a task button lifts out of the bar, out of 255, and how much
 * more of that the top of it gets than the foot. */
#define LIFT       0xDCE6F5U
#define LIFT_ON      46U
#define LIFT_OFF     16U
#define LIFT_GRADE   26U

struct rsd_rect rsd_panel_bounds(uint32_t width, uint32_t height)
{
    struct rsd_rect box = { 0U, 0U, 0U, 0U };

    if (height < RSD_PANEL_HEIGHT) {
        return box;
    }
    box.x = 0U;
    box.y = height - RSD_PANEL_HEIGHT;
    box.width = width;
    box.height = RSD_PANEL_HEIGHT;
    return box;
}

struct rsd_rect rsd_panel_launcher(uint32_t width, uint32_t height)
{
    struct rsd_rect box = rsd_panel_bounds(width, height);

    if (box.width < RSD_PANEL_LAUNCHER) {
        box.width = 0U;
        return box;
    }
    box.width = RSD_PANEL_LAUNCHER;
    return box;
}

/* Where the tray's icons start. The clock is right-aligned against the
 * end of the bar and the tray sits just left of it, so both are
 * measured back from the edge and the task buttons get what is left. */
static uint32_t tray_left(uint32_t width)
{
    uint32_t want = CLOCK_ROOM + RSD_ICON_COUNT_TRAY * TRAY_STEP + PAD;

    return width > want ? width - want : 0U;
}

bool rsd_panel_task_bounds(uint32_t width, uint32_t height,
    uint32_t index, uint32_t count, struct rsd_rect *out)
{
    struct rsd_rect bar = rsd_panel_bounds(width, height);
    uint32_t left = RSD_PANEL_LAUNCHER + PAD;
    uint32_t right = tray_left(bar.width);
    uint32_t span;
    uint32_t each;

    if (out == NULL || count == 0U || index >= count || right <= left) {
        return false;
    }
    span = right - left - PAD;
    each = span / count;
    if (each > TASK_WIDE) {
        each = TASK_WIDE;
    }
    if (each < 44U) {
        return false;
    }
    out->x = left + index * each;
    out->y = bar.y + PAD;
    out->width = each - PAD;
    out->height = bar.height - PAD * 2U;
    return true;
}


/* One cell of the mark's ink plane: a hex digit indexing
 * rsd_logo_ink. Index 0 is not a colour - it means the cell was not
 * part of the drawing. */
static uint32_t ink_at(char code)
{
    uint32_t index;

    if (code >= '0' && code <= '9') {
        index = (uint32_t)(code - '0');
    } else if (code >= 'A' && code <= 'F') {
        index = (uint32_t)(code - 'A') + 10U;
    } else {
        return 0U;
    }
    return index == 0U || index >= RSD_LOGO_INKS
        ? 0U : rsd_logo_ink[index];
}

/*
 * THE MARK, on the launcher.
 *
 * The owner's drawing, the same table rsdfetch prints - its own colours,
 * nothing classified and nothing averaged. Each cell is one pixel wide
 * and two tall, because the art was measured in character cells and a
 * character cell is about twice as tall as it is wide; at one pixel
 * square the mark comes out squashed.
 */
static void mark(struct rsd_surface *surface, struct rsd_rect clip,
    struct rsd_rect box)
{
    uint32_t left;
    uint32_t top;
    uint32_t row;

    if (box.width < RSD_LOGO_COLUMNS || box.height < RSD_LOGO_ROWS * 2U) {
        return;
    }
    left = box.x + (box.width - RSD_LOGO_COLUMNS) / 2U;
    top = box.y + (box.height - RSD_LOGO_ROWS * 2U) / 2U;

    for (row = 0U; row < RSD_LOGO_ROWS; ++row) {
        const char *plane = rsd_logo_at[row];
        uint32_t column;

        for (column = 0U; column < RSD_LOGO_COLUMNS; ++column) {
            uint32_t colour;

            if (plane[column] == '\0') {
                break;
            }
            colour = ink_at(plane[column]);
            if (colour == 0U) {
                continue;
            }
            rsd_surface_plot(surface, clip, left + column,
                               top + row * 2U, colour);
            rsd_surface_plot(surface, clip, left + column,
                               top + row * 2U + 1U, colour);
        }
    }
}

/* The glass, one row at a time, so the bar has a section rather than a
 * tint. See the note at the top of this file. */
static void glass(struct rsd_surface *surface, struct rsd_rect bar)
{
    uint32_t y;

    for (y = 0U; y < bar.height; ++y) {
        struct rsd_rect row = bar;
        uint32_t half = bar.height / 2U;
        uint32_t alpha;

        row.y = bar.y + y;
        row.height = 1U;
        if (y < half) {
            alpha = SHEER_TOP - (SHEER_TOP - SHEER_MID) * y / (half ? half : 1U);
        } else {
            alpha = SHEER_MID + (SHEER_FOOT - SHEER_MID) * (y - half)
                    / (half ? half : 1U);
        }
        rsd_surface_wash(surface, bar, row, GLASS, alpha);
    }
}

void rsd_panel_draw(struct rsd_surface *surface,
    const struct rsd_window *windows, const bool *used, uint32_t slots,
    uint32_t desktop, const char *clock, const char *date)
{
    static const uint32_t TRAY[RSD_ICON_COUNT_TRAY] = {
        RSD_ICON_SOUND, RSD_ICON_NETWORK,
        RSD_ICON_POWER, RSD_ICON_SETTINGS
    };
    struct rsd_rect bar;
    struct rsd_rect box;
    uint32_t shown = 0U;
    uint32_t drawn = 0U;
    uint32_t slot;
    uint32_t x;
    uint32_t mid;

    if (!rsd_surface_valid(surface)) {
        return;
    }
    bar = rsd_panel_bounds(surface->width, surface->height);
    if (bar.width == 0U) {
        return;
    }
    glass(surface, bar);

    /* The lit top edge, and a darker line under it so the highlight has
     * something to sit on. Both mixed with what is already there, so
     * the edge follows the picture behind the bar the way the rest of
     * the glass does. */
    for (x = bar.x; x < bar.x + bar.width; ++x) {
        rsd_surface_plot(surface, bar, x, bar.y,
            rsd_oxy_mix(rsd_surface_read(surface, x, bar.y), EDGE,
                          EDGE_ALPHA));
        rsd_surface_plot(surface, bar, x, bar.y + 1U,
            rsd_oxy_mix(rsd_surface_read(surface, x, bar.y + 1U),
                          GLASS, EDGE_UNDER));
    }

    /* The launcher. No frame round it: the mark IS the button, the way
     * a panel launcher has been since these bars existed. */
    mark(surface, bar, rsd_panel_launcher(surface->width,
                                            surface->height));

    for (slot = 0U; slot < slots; ++slot) {
        if (used[slot] && windows[slot].desktop == desktop) {
            ++shown;
        }
    }

    for (slot = 0U; slot < slots; ++slot) {
        if (!used[slot] || windows[slot].desktop != desktop) {
            continue;
        }
        if (rsd_panel_task_bounds(surface->width, surface->height,
                                    drawn, shown, &box)) {
            /*
             * A TASK BUTTON ON A DARK BAR IS DARK.
             *
             * These were Oxygen slabs in the WINDOW's colour - warm
             * light grey - sitting on a bar that is nearly black, which
             * is three pale rectangles pasted onto the glass. A panel of
             * this period does not put window-coloured buttons on a dark
             * bar: the button is the same glass, lifted, with a rim
             * where the light catches it.
             *
             * So it is a wash rather than a fill. What is behind the bar
             * still shows through the button, the raised one shows a
             * little more of it, and the rim is the same trick the top
             * edge uses.
             */
            bool down = windows[slot].active && !windows[slot].minimised;
            uint32_t pen = box.x + 8U;
            uint32_t y;

            for (y = box.y; y < box.y + box.height; ++y) {
                uint32_t lift = down ? LIFT_ON : LIFT_OFF;
                uint32_t along = (y - box.y) * 255U
                    / (box.height ? box.height : 1U);

                /* Lit from above, like everything else here: the top of
                 * the button lets more through than the foot. */
                lift = lift + (255U - along) * LIFT_GRADE / 255U;
                for (x = box.x; x < box.x + box.width; ++x) {
                    uint32_t under;

                    if (rsd_oxy_outside(box, x, y, 4U, false)) {
                        continue;
                    }
                    under = rsd_surface_read(surface, x, y);
                    if (rsd_oxy_outside(box, x, y, 3U, false)) {
                        rsd_surface_plot(surface, bar, x, y,
                            rsd_oxy_mix(under, EDGE,
                                          down ? 120U : 60U));
                        continue;
                    }
                    rsd_surface_plot(surface, bar, x, y,
                        rsd_oxy_mix(under, LIFT, lift));
                }
            }

            /* The icon says what the window IS at a glance; the title
             * says which one it is. A button with only a title is a
             * button you have to read. */
            if (box.width > RSD_ICON_SIZE + 24U) {
                rsd_oxy_blit(surface, box, pen,
                     box.y + (box.height - RSD_ICON_SIZE) / 2U,
                     rsd_icons[windows[slot].icon], RSD_ICON_SIZE);
                pen += RSD_ICON_SIZE + 6U;
            }
            rsd_font_draw(surface, box, pen,
                box.y + (box.height + rsd_font_ascent()) / 2U - 1U,
                windows[slot].title,
                windows[slot].minimised ? INK_DIM : INK);
        }
        ++drawn;
    }

    /* The tray, right-aligned before the clock. */
    mid = bar.y + (bar.height - RSD_ICON_SIZE) / 2U;
    for (x = 0U; x < RSD_ICON_COUNT_TRAY; ++x) {
        rsd_oxy_blit(surface, bar, tray_left(bar.width) + x * TRAY_STEP,
                       mid, rsd_icons[TRAY[x]], RSD_ICON_SIZE);
    }

    /*
     * The clock, at the very end: the time over the date, which is what
     * a digital clock on one of these bars looks like.
     *
     * Both strings come from the shell rather than being worked out
     * here, because this machine's wall clock is the shell's to know
     * about and a bar that invented a time would be showing something
     * the machine does not.
     */
    if (clock != NULL) {
        uint32_t span = rsd_font_width(clock);
        uint32_t right = bar.x + bar.width - PAD * 2U;

        rsd_font_draw(surface, bar, right > span ? right - span : bar.x,
                        bar.y + 17U, clock, INK);
    }
    if (date != NULL) {
        uint32_t span = rsd_font_width(date);
        uint32_t right = bar.x + bar.width - PAD * 2U;

        rsd_font_draw(surface, bar, right > span ? right - span : bar.x,
                        bar.y + 31U, date, INK_DIM);
    }
}
