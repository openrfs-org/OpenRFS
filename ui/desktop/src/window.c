/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd_desktop/window.h>

#include <rsd_desktop/font.h>
#include <rsd_desktop/oxygen.h>
#include <rsd_desktop/theme.h>


struct rsd_rect rsd_window_title(const struct rsd_window *window)
{
    struct rsd_rect box = { 0U, 0U, 0U, 0U };

    if (window == NULL) {
        return box;
    }
    box.x = window->frame.x + RSD_BORDER;
    box.y = window->frame.y + RSD_BORDER;
    box.width = window->frame.width > RSD_BORDER * 2U ?
        window->frame.width - RSD_BORDER * 2U : 0U;
    box.height = RSD_TITLE_HEIGHT;
    return box;
}

struct rsd_rect rsd_window_client(const struct rsd_window *window)
{
    struct rsd_rect title = rsd_window_title(window);
    struct rsd_rect box = { 0U, 0U, 0U, 0U };
    uint32_t chrome = RSD_BORDER * 2U + RSD_TITLE_HEIGHT;

    if (window == NULL) {
        return box;
    }
    box.x = title.x;
    box.y = title.y + title.height;
    box.width = title.width;
    box.height = window->frame.height > chrome ?
        window->frame.height - chrome : 0U;
    return box;
}

void rsd_window_set_title(struct rsd_window *window, const char *text)
{
    uint32_t at = 0U;

    if (window == NULL) {
        return;
    }
    while (text != NULL && text[at] != '\0' &&
            at + 1U < RSD_TITLE_BYTES) {
        window->title[at] = text[at];
        ++at;
    }
    window->title[at] = '\0';
}

/*
 * THE BUTTONS, as Oxygen puts them on a title bar.
 *
 * They were square, the full height of the bar and flush against its
 * end - fvwm's arrangement, where a bar ENDS in buttons and there is no
 * air anywhere. Oxygen's are round orbs with a gap between them and a
 * margin before the corner, which is why a KDE 4 title bar reads as
 * three objects sitting on a surface rather than as a row of keys.
 *
 * The sizes are in window.h.
 */

/*
 * OXYGEN'S 21-UNIT SPACE.
 *
 * oxygenbutton.cpp draws every mark with `painter->scale(width / 21, ...)`
 * and then names its points in whole and half units between 7.5 and 13.5.
 * Rather than convert those to pixels by hand and lose the quotation,
 * this scales them: the numbers below are the source's, in tenths so
 * there is no floating point, and unit() turns one into a pixel.
 */
#define BUTTON_UNITS 210U       /* 21 units, in tenths */

static uint32_t unit(uint32_t box, uint32_t tenths)
{
    return (tenths * box + BUTTON_UNITS / 2U) / BUTTON_UNITS;
}

/*
 * ONE DEFINITION of where each button is, used to draw it and to answer a
 * press on it. Two definitions drift, and the way that shows up is a
 * close button that closes when you click slightly to the left of it.
 */
bool rsd_window_button_bounds(const struct rsd_window *window,
    enum rsd_window_button which, struct rsd_rect *out)
{
    struct rsd_rect title = rsd_window_title(window);
    uint32_t side = RSD_BUTTON_SIDE;
    uint32_t step = side + RSD_BUTTON_GAP;
    uint32_t right;

    if (window == NULL || out == NULL
            || title.width < step * 3U + RSD_BUTTON_MARGIN
            || title.height < side) {
        return false;
    }
    right = title.x + title.width - RSD_BUTTON_MARGIN;
    /* Centred in the bar rather than filling it: the orbs sit ON the
     * title bar, so there is air above and below them as well. */
    out->y = title.y + (title.height - side) / 2U;
    out->width = side;
    out->height = side;
    switch (which) {
    case RSD_WINDOW_CLOSE:
        out->x = right - side;
        return true;
    case RSD_WINDOW_MAXIMISE:
        out->x = right - side - step;
        return true;
    case RSD_WINDOW_MINIMISE:
        out->x = right - side - step * 2U;
        return true;
    default:
        return false;
    }
}

/*
 * THE ORB, which is what an Oxygen title-bar button is.
 *
 * From oxygendecohelper.cpp windecoButton(): inside an 18-unit box, a
 * circle 12.33 units across with its top at 1.665, filled with a VERTICAL
 * gradient from calcLightColor at the top to calcDarkColor at the bottom,
 * and rung with a 0.7-unit outline on the same two colours. Sunken swaps
 * the ends.
 *
 * Note what that is not: it is not the slab gradient a button face gets
 * inside a window. A slab's gradient is taller than the widget and you
 * see a slice of its middle; this one is a sphere lit from above and you
 * see all of it. Drawing the title buttons as little slabs is what made
 * them read as keys off a keyboard.
 *
 * The edge is antialiased, because a twelve-pixel circle of hard pixels
 * is a dodecagon and reads as one. Coverage comes out of the squared
 * distance so there is no root here either: between (r-1)^2 and r^2 the
 * pixel is on the limb, and how far along tells you how much of it is
 * inside.
 */
static void orb(struct rsd_surface *surface, struct rsd_rect clip,
    struct rsd_rect box, uint32_t base, bool sunken)
{
    /*
     * 12.33 across and 1.665 down, both of the 18-unit space
     * windecoButton() switches to for the slab - so in the 21-unit space
     * everything else here is named in, 12.33 * 21 / 18 = 14.4 across
     * and 1.665 * 21 / 18 = 1.9 down. In tenths, which is what unit()
     * takes.
     */
    uint32_t diameter = unit(box.height, 144U);
    uint32_t top = box.y + unit(box.height, 19U);
    uint32_t left = box.x + (box.width - diameter) / 2U;
    uint32_t radius = diameter / 2U;
    uint32_t light = rsd_oxy_light(base);
    uint32_t dark = rsd_oxy_dark(base);
    uint32_t centre_x = left + radius;
    uint32_t centre_y = top + radius;
    uint32_t x;
    uint32_t y;

    if (diameter < 4U) {
        return;
    }
    for (y = top; y < top + diameter; ++y) {
        /* The fill's gradient, and the ring's, both run down the orb;
         * the ring's runs over twice the height, so it is lighter at the
         * bottom than the fill it surrounds. */
        uint32_t along = (y - top) * 255U / diameter;
        uint32_t face = sunken ? rsd_oxy_mix(dark, light, along)
                               : rsd_oxy_mix(light, dark, along);
        uint32_t ring = rsd_oxy_mix(light, dark, along / 2U);

        for (x = left; x < left + diameter; ++x) {
            uint32_t dx = x >= centre_x ? x - centre_x : centre_x - x;
            uint32_t dy = y >= centre_y ? y - centre_y : centre_y - y;
            uint32_t squared = dx * dx + dy * dy;
            uint32_t inner = (radius - 1U) * (radius - 1U);
            uint32_t limb = radius * radius;
            uint32_t colour;
            uint32_t alpha = 255U;

            if (squared > limb) {
                /*
                 * THE SEAT UNDER THE ORB. windecoButton() draws a shadow
                 * before it draws the button - drawShadow(calcShadowColor
                 * (color), 21) - and without it the orb is a circle of
                 * nearly the title bar's own colour on the title bar,
                 * which is to say invisible. Two pixels of it, fading.
                 */
                uint32_t out = squared - limb;
                uint32_t reach = radius * 2U + 4U;

                if (out > reach) {
                    continue;
                }
                rsd_surface_plot(surface, clip, x, y,
                    rsd_blend(rsd_surface_read(surface, x, y),
                                rsd_oxy_shadow(base),
                                (reach - out) * 44U / reach));
                continue;
            }
            if (squared > inner) {
                /* On the limb: the ring's colour, and only part of the
                 * pixel is covered. */
                colour = ring;
                alpha = 255U - (squared - inner) * 255U / (limb - inner);
            } else if (squared + radius > inner) {
                colour = ring;
            } else {
                colour = face;
            }
            rsd_surface_plot(surface, clip, x, y,
                alpha == 255U ? colour
                    : rsd_blend(rsd_surface_read(surface, x, y),
                                  colour, alpha));
        }
    }
}

/*
 * A MARK'S STROKE. Every mark in oxygenbutton.cpp is a polyline of
 * 45-degree segments between points on a 21-unit grid, so this is all the
 * line drawing a title bar needs: one step in x per step in y, or a
 * straight run.
 */
static void stroke(struct rsd_surface *surface, struct rsd_rect clip,
    uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t ink)
{
    uint32_t steps = x1 > x0 ? x1 - x0 : x0 - x1;
    uint32_t down = y1 > y0 ? y1 - y0 : y0 - y1;
    uint32_t at;

    if (down > steps) {
        steps = down;
    }
    for (at = 0U; at <= steps; ++at) {
        uint32_t x = x1 > x0 ? x0 + at * (x1 - x0) / steps
                             : x0 - at * (x0 - x1) / steps;
        uint32_t y = y1 > y0 ? y0 + at * (y1 - y0) / steps
                             : y0 - at * (y0 - y1) / steps;

        rsd_surface_plot(surface, clip, x, y, ink);
        /* The pen is 1.2 units wide with a round cap, which at this size
         * is a hair over one pixel. One pixel of weight below it is what
         * keeps a diagonal from looking like a dotted line. */
        rsd_surface_plot(surface, clip, x, y + 1U, ink);
    }
}

/*
 * THE MARKS THEMSELVES, quoted from oxygenbutton.cpp drawIcon():
 *
 *   Minimize  polyline (7.5, 9.5) (10.5, 12.5) (13.5, 9.5)   - down
 *   Maximize  polyline (7.5, 11.5) (10.5, 8.5) (13.5, 11.5)  - up
 *     maximised: polygon (7.5, 10.5) (10.5, 7.5) (13.5, 10.5) (10.5, 13.5)
 *   Close     line (7.5, 7.5) (13.5, 13.5) and (13.5, 7.5) (7.5, 13.5)
 *
 * Chevrons, not Motif's little squares. That is the whole difference
 * between a KDE 4 title bar and an X11 one, and the squares are what were
 * here before.
 */
static void mark(struct rsd_surface *surface, struct rsd_rect clip,
    struct rsd_rect box, enum rsd_window_button which, bool maximised,
    uint32_t ink)
{
    uint32_t side = box.height;
    uint32_t l = box.x + unit(side, 75U);
    uint32_t m = box.x + unit(side, 105U);
    uint32_t r = box.x + unit(side, 135U);

    switch (which) {
    case RSD_WINDOW_MINIMISE:
        stroke(surface, clip, l, box.y + unit(side, 95U),
               m, box.y + unit(side, 125U), ink);
        stroke(surface, clip, m, box.y + unit(side, 125U),
               r, box.y + unit(side, 95U), ink);
        break;
    case RSD_WINDOW_MAXIMISE:
        if (maximised) {
            /* The restore mark is a filled diamond, which is the one
             * mark Oxygen fills rather than strokes. Drawn as its four
             * edges plus the two diagonals of its inside, which at this
             * size is the whole of it. */
            stroke(surface, clip, l, box.y + unit(side, 105U),
                   m, box.y + unit(side, 75U), ink);
            stroke(surface, clip, m, box.y + unit(side, 75U),
                   r, box.y + unit(side, 105U), ink);
            stroke(surface, clip, l, box.y + unit(side, 105U),
                   m, box.y + unit(side, 135U), ink);
            stroke(surface, clip, m, box.y + unit(side, 135U),
                   r, box.y + unit(side, 105U), ink);
        } else {
            stroke(surface, clip, l, box.y + unit(side, 115U),
                   m, box.y + unit(side, 85U), ink);
            stroke(surface, clip, m, box.y + unit(side, 85U),
                   r, box.y + unit(side, 115U), ink);
        }
        break;
    case RSD_WINDOW_CLOSE:
        stroke(surface, clip, l, box.y + unit(side, 75U),
               r, box.y + unit(side, 135U), ink);
        stroke(surface, clip, r, box.y + unit(side, 75U),
               l, box.y + unit(side, 135U), ink);
        break;
    default:
        break;
    }
}

static void buttons(struct rsd_surface *surface, struct rsd_rect title,
    const struct rsd_window *window, uint32_t ink, uint32_t base,
    uint32_t contrast)
{
    static const enum rsd_window_button ORDER[] = {
        RSD_WINDOW_MINIMISE, RSD_WINDOW_MAXIMISE, RSD_WINDOW_CLOSE
    };
    struct rsd_rect box;
    uint32_t at;

    for (at = 0U; at < sizeof(ORDER) / sizeof(ORDER[0]); ++at) {
        if (!rsd_window_button_bounds(window, ORDER[at], &box)) {
            continue;
        }
        orb(surface, title, box, base, false);
        /*
         * TWICE, one pixel apart, which is how every mark on an Oxygen
         * button is drawn:
         *
         *     painter->translate(0, 1.5);
         *     painter->setPen(QPen(calcLightColor(base), ...));
         *     drawIcon(painter);
         *     painter->translate(0, -1.5);
         *     painter->setPen(QPen(color, ...));
         *     drawIcon(painter);
         *
         * The lower copy is the light one. It is not a shadow - it is a
         * highlight below the mark, which is what makes the mark look cut
         * INTO the orb rather than drawn on it.
         */
        {
            struct rsd_rect under = box;

            under.y += 1U;
            mark(surface, title, under, ORDER[at], window->maximised,
                 contrast);
        }
        mark(surface, title, box, ORDER[at], window->maximised, ink);
    }
}

void rsd_window_draw(struct rsd_surface *surface,
    const struct rsd_window *window)
{
    struct rsd_rect title;
    struct rsd_rect client;
    uint32_t ground;
    uint32_t lit;
    uint32_t shade;
    uint32_t ink;

    if (window == NULL || !rsd_surface_valid(surface)) {
        return;
    }
    title = rsd_window_title(window);
    client = rsd_window_client(window);
    rsd_oxy_cast(surface, window->frame, window->active);
    ground = window->active ? RSD_FRAME_ACTIVE : RSD_FRAME_IDLE;
    lit = window->active ? RSD_FRAME_ACTIVE_HI : RSD_FRAME_IDLE_HI;
    shade = window->active ? RSD_FRAME_ACTIVE_LO : RSD_FRAME_IDLE_LO;
    ink = window->active ? RSD_FRAME_INK : RSD_FRAME_INK_DIM;

    /*
     * THE BORDER AND THE TITLE BAR, WHICH ARE ONE SHEET.
     *
     * fvwm drew these as two raised bars with a bevel round each. Oxygen
     * draws neither: the whole decoration is a single lit surface with
     * the client punched out of it, and the title is text standing on
     * the top of that surface rather than a bar carrying it.
     *
     * Every pixel takes its colour from rsd_oxy_window() called with
     * the WHOLE FRAME's top and height - never the strip's own. That is
     * the y_shift idea out of renderWindowBackground(), and it is the
     * reason the border down the left of a window is the same shade as
     * the title bar beside it instead of restarting its own gradient.
     *
     * THE CLIENT IS LEFT ALONE, still. This used to be one fill of the
     * frame with the client painted over afterwards, and that is what
     * broke pseudo-transparency: a sheer terminal reads the framebuffer
     * back, and what it found there was its own frame already covering
     * the desktop. So the loop below skips the client rect, and what is
     * inside belongs to whatever is behind the window until the
     * application covers it.
     */
    {
        uint32_t radius = RSD_OXY_RADIUS;
        uint32_t x;
        uint32_t y;

        for (y = window->frame.y;
             y < window->frame.y + window->frame.height; ++y) {
            uint32_t row = rsd_oxy_window(ground, y, window->frame.y,
                                            window->frame.height);

            for (x = window->frame.x;
                 x < window->frame.x + window->frame.width; ++x) {
                uint32_t colour;

                if (rsd_rect_contains(client, x, y)) {
                    continue;
                }
                /* The pool of light on the top edge, over the vertical
                 * gradient - see rsd_oxy_pool(). It is what makes the
                 * middle of a wide title bar brighter than its ends. */
                colour = rsd_oxy_pool(ground, row, x, y, window->frame);
                if (rsd_oxy_outside(window->frame, x, y, radius, true)) {
                    continue;
                }
                if (rsd_oxy_outside(window->frame, x, y,
                                      radius - 1U, true)
                    || x == window->frame.x || y == window->frame.y
                    || x + 1U == window->frame.x + window->frame.width
                    || y + 1U == window->frame.y + window->frame.height) {
                    colour = shade;
                } else if (x == window->frame.x + 1U
                           || y == window->frame.y + 1U
                           || x + 2U == window->frame.x + window->frame.width
                           || y + 2U == window->frame.y
                                        + window->frame.height) {
                    colour = rsd_oxy_mix(row, lit, 150U);
                }
                rsd_surface_plot(surface, window->frame, x, y, colour);
            }
        }

        /* Where the client meets the sheet, one dark line - the view is
         * sunk into the window, not sitting on it. */
        for (x = client.x; x < client.x + client.width; ++x) {
            rsd_surface_plot(surface, window->frame, x, client.y - 1U,
                               rsd_oxy_dark(ground));
        }
        for (y = client.y - 1U; y < client.y + client.height; ++y) {
            rsd_surface_plot(surface, window->frame, client.x - 1U, y,
                               rsd_oxy_dark(ground));
            rsd_surface_plot(surface, window->frame,
                               client.x + client.width, y,
                               rsd_oxy_dark(ground));
        }
    }

    /*
     * THE CAPTION: centred, and EMBOSSED.
     *
     * Oxygen paints it twice - renderTitleText() sets the contrast
     * colour, translates down one pixel, draws, translates back and draws
     * again in the real colour. One pixel, always, whatever the font
     * size. It is the difference between a name lying on the glass and a
     * name standing on it, and it is why a KDE 4 caption reads as heavy
     * without being bold.
     *
     * Centring is Oxygen's default too (AlignCenterFullWidth), so this
     * did not have to change - what changed is that the words are now
     * antialiased DejaVu Sans rather than a bitmap face, and that is
     * most of what makes the bar look like a bar of its year.
     */
    {
        uint32_t span = rsd_font_width(window->title);
        uint32_t pen = title.width > span ?
            title.x + (title.width - span) / 2U : title.x + 2U;
        uint32_t baseline = title.y + (title.height
            + rsd_font_ascent()) / 2U - 1U;

        rsd_font_draw_embossed(surface, title, pen, baseline,
                                 window->title, ink, lit);
    }
    buttons(surface, title, window, ink, ground, lit);
    (void)shade;
}
