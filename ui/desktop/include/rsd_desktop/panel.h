/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_PANEL_H
#define RSD_PANEL_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd_desktop/surface.h>
#include <rsd_desktop/window.h>

/*
 * THE PANEL.
 *
 * There was none: a previous pass took the bar, the dock and the tray
 * out entirely and left the root with a menu on it, which is how a bare
 * fvwm session works. That was right for what the desktop was copying
 * then and is wrong for what it is copying now - a 2010 desktop has a
 * bar along the bottom and everything about the way you move around it
 * assumes one.
 *
 * It is glass in the only sense this machine can manage. There is no
 * compositor and no alpha channel, so the bar reads the framebuffer
 * back and mixes with what it finds - the same pseudo-transparency an X
 * terminal has always had, and the same call the sheer terminal uses.
 * What is behind the bar is the wallpaper, so the bar is genuinely the
 * colour of the sky above it rather than a grey that was picked to look
 * like it.
 */

/* Tall enough for a 32-pixel icon with a little air, which is what the
 * era settled on. */
#define RSD_PANEL_HEIGHT 40U

/* The launcher at the left end, with the mark on it. */
#define RSD_PANEL_LAUNCHER 58U

/* How much of what is behind the bar shows through, out of 255. Low
 * enough to read text over, high enough that moving a window under it
 * visibly changes it - which is the whole point of doing it this way
 * rather than filling a rectangle. */
#define RSD_PANEL_SHEER 62U

/* The clock's room is taken off the bar before the task buttons are
 * measured, so a tenth window shortens the buttons instead of running
 * under the time. */
#define CLOCK_ROOM 84U

/* A task button never grows past this, however few windows there are.
 * A bar with one window on it and a button four hundred pixels wide
 * looks broken rather than roomy. */
#define TASK_WIDE 168U

/* Four in the tray - sound, network, power, settings - and the step
 * between them. A 2010 bar has about this many and a clock; a tray that
 * grows without limit is a tray nobody reads. */
#define RSD_ICON_COUNT_TRAY 4U
#define TRAY_STEP 26U

struct rsd_rect rsd_panel_bounds(uint32_t width, uint32_t height);
struct rsd_rect rsd_panel_launcher(uint32_t width, uint32_t height);

/* One definition of where a task button is, used to draw it and to
 * answer a press on it. Two definitions drift, and the way that shows
 * up is a button that raises the window next to the one you clicked. */
bool rsd_panel_task_bounds(uint32_t width, uint32_t height,
    uint32_t index, uint32_t count, struct rsd_rect *out);

void rsd_panel_draw(struct rsd_surface *surface,
    const struct rsd_window *windows, const bool *used, uint32_t slots,
    uint32_t desktop, const char *clock, const char *date);

#endif /* RSD_PANEL_H */
