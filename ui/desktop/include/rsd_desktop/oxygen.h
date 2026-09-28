/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_OXYGEN_H
#define RSD_OXYGEN_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd_desktop/surface.h>

/*
 * OXYGEN, WHICH IS WHERE THE GLASS COMES FROM.
 *
 * Read off KDE's own kstyle/oxygenstylehelper.cpp and
 * liboxygen/oxygenhelper.cpp. See docs/REFERENCES.md for the quotations.
 *
 * THE ONE IDEA WORTH TAKING. Oxygen does not give each widget its own
 * gradient. It gives the WINDOW a gradient and then every widget inside
 * samples that same gradient at its own absolute y - which is what the
 * `y_shift` argument to renderWindowBackground() is for.
 *
 * That single decision is most of why a KDE 4 window looks like one
 * sheet of lit glass instead of a stack of separately shaded boxes. A
 * button near the top of a window is lighter than the same button near
 * the bottom, because the light is falling on the window, not on the
 * button. Every function here that takes (y, win_y, win_h) is taking
 * that idea seriously: pass the widget's absolute position and the
 * window it lives in, never its own local coordinates.
 *
 * The fvwm bevel this replaces had no gradient anywhere and a separately
 * raised title bar. Oxygen has neither: the title bar is not a bar, it
 * is the top of the same sheet.
 *
 * Integer arithmetic throughout. This is freestanding code with no
 * floating point, so the ratios quoted from the source as 0.2 and 0.6
 * appear here as fractions of a span.
 */

/* Per channel: a + (b - a) * t / 255. */
uint32_t rsd_oxy_mix(uint32_t a, uint32_t b, uint32_t t);

/* Every channel scaled by num/den and clamped. The three shades below
 * are this with the factors Oxygen's calcLightColor, calcDarkColor and
 * calcShadowColor arrive at through KColorScheme.
 *
 * Those go through KColorScheme::shade() with a contrast setting, whose
 * exact curve is not reproduced here - what is reproduced is the
 * structure: one base colour, three shades off it, everything else
 * derived. The factors below were chosen to sit where KDE 4's defaults
 * land, and they are in one place so they can be moved together. */
uint32_t rsd_oxy_shade(uint32_t colour, uint32_t num, uint32_t den);
uint32_t rsd_oxy_light(uint32_t colour);
uint32_t rsd_oxy_dark(uint32_t colour);
uint32_t rsd_oxy_shadow(uint32_t colour);

/*
 * The window's own gradient, sampled at an absolute y.
 *
 * From oxygenstylehelper.cpp: the gradient runs from the top of the
 * window down to
 *
 *     splitY = min(200, (3 * height) / 4)
 *
 * and below that line it is flat. A tall window is lit at the top and
 * settles - it does not keep getting darker forever, which is what a
 * naive top-to-bottom gradient does and why those always look wrong on
 * a maximised window.
 */
uint32_t rsd_oxy_window(uint32_t base, uint32_t y, uint32_t win_y,
    uint32_t win_h);

/*
 * A button's face - Oxygen calls it a slab.
 *
 * The gradient is deliberately taller than the button:
 *
 *     raised: from top - 0.2h to bottom + 0.4h,
 *             stop 0.0 = calcLightColor(colour), stop 0.6 = colour
 *     sunken: from top to bottom + h,
 *             stop 0.0 = colour, stop 1.0 = calcLightColor(colour)
 *
 * So what you see is a slice out of the middle of a bigger curve. That
 * is why the highlight on an Oxygen button never reads as a stripe
 * across it: you are never looking at the whole gradient.
 */
uint32_t rsd_oxy_slab(uint32_t base, uint32_t y, uint32_t top,
    uint32_t height, bool sunken);

/*
 * The pool of light on the window's top edge, laid over whatever the
 * vertical gradient gave that pixel. `window` is the WHOLE window, not
 * the widget: the pool belongs to the window and a widget near the top
 * of one is simply inside it.
 *
 * Returns `under` unchanged outside the pool, so it can be applied to
 * every pixel of a frame without a test around it.
 */
uint32_t rsd_oxy_pool(uint32_t base, uint32_t under, uint32_t x,
    uint32_t y, struct rsd_rect window);

/*
 * Is (x, y) cut off by a rounded corner?
 *
 * `top_only` rounds the top two corners and leaves the bottom square,
 * which is what a window decoration does - the bottom of a window meets
 * the screen edge often enough that rounding it looks like a mistake.
 *
 * Radii in the source sit at 3.5, 2.5 and 4.0 px; this takes one in
 * whole pixels because a framebuffer has no half.
 */
bool rsd_oxy_outside(struct rsd_rect box, uint32_t x, uint32_t y,
    uint32_t radius, bool top_only);

/*
 * The lit sheet, drawn.
 *
 * Fills `box` with the window gradient, rounds the corners it is told
 * to, and edges it: one dark line outside, one light line just inside.
 * That pair is Oxygen's whole relief - there is no bevel, no second
 * ring and no handles.
 */
void rsd_oxy_sheet(struct rsd_surface *surface, struct rsd_rect clip,
    struct rsd_rect box, uint32_t base, uint32_t radius, bool top_only);

/* Oxygen's radii sit at 3.5, 2.5 and 4.0 px in the source; a
 * framebuffer has no half. */
#define RSD_OXY_BUTTON_RADIUS 4U

/*
 * A button face: the slab, the rounded corners and the one dark line
 * that holds its edge. `sunken` is a pressed button, or a toggle that
 * is on.
 */
void rsd_oxy_button(struct rsd_surface *surface, struct rsd_rect clip,
    struct rsd_rect box, uint32_t base, bool sunken);

/*
 * The shadow a floating surface casts, and the ACTIVE one is a blue
 * glow: Oxygen's ActiveShadow is InnerColor 112,239,255 over OuterColor
 * 84,167,240 at forty pixels, not a grey shadow at all.
 *
 * Takes a box rather than a window, because a menu casts one too.
 */
void rsd_oxy_cast(struct rsd_surface *surface, struct rsd_rect box,
    bool active);

/*
 * An icon, composited.
 *
 * `argb` is side*side pixels of 0xAARRGGBB out of src/rsd_icons.h.
 * The alpha matters: a 22x22 icon is mostly nothing, and writing it
 * over rather than mixing puts a grey square round every one.
 *
 * Generic rather than reaching for the icon table itself, so the panel
 * and the file manager share one blit instead of growing one each.
 */
void rsd_oxy_blit(struct rsd_surface *surface, struct rsd_rect clip,
    uint32_t left, uint32_t top, const uint32_t *argb, uint32_t side);

#endif /* RSD_OXYGEN_H */
