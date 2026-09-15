/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_DE_PANEL_H
#define TRAIT_DE_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <trait/de/surface.h>

/*
 * THE TRAIT OS PANEL: lxpanel's bar, in C.
 *
 * The same panel this repository's index.html draws in a browser, written
 * against a linear framebuffer instead.  The JavaScript is the SPEC, not
 * the source - nothing here is a transliteration of it - and every number
 * below carries the Debian file it was read out of, exactly as the
 * JavaScript does.
 *
 *     lxde-common 0.99.2-4   /etc/xdg/lxpanel/LXDE/panels/panel
 *         Global { edge=bottom  height=26  fontcolor=#ffffff
 *                  background=1 }
 *         space 2 | menu | launchbar | space 4 | wincmd | space 4 |
 *         pager | space 4 | taskbar(expand=1) | cpu | volume | tray |
 *         dclock(%R) | launchbar
 *
 *     lxpanel-data 0.11.1-2  images/background.png  (1x26, tiled across)
 *     lxpanel 0.11.1         plugins/cpu/cpu.c      (40x26, border 2,
 *                                                    gdk_color_parse
 *                                                    ("green") = #00FF00)
 *
 * The panel owns no windows.  It is told what exists through
 * trait_panel_set_task(); whatever composites it decides what that
 * means.
 */

#define TRAIT_PANEL_HEIGHT 26U       /* Global { height=26 } */
#define TRAIT_PANEL_MAX_TASKS 12U
#define TRAIT_PANEL_LABEL_BYTES 32U
#define TRAIT_PANEL_MAX_DESKTOPS 4U

/* taskbar { MaxTaskWidth=150 } in the profile's own plugin block. */
#define TRAIT_PANEL_MAX_TASK_WIDTH 150U

/* cpu.c: the widget is 40 wide and BORDER_SIZE is 2, so the graph inside
 * it is 36 columns by 22 rows and each column is one sample. */
#define TRAIT_PANEL_CPU_WIDTH 40U
#define TRAIT_PANEL_CPU_BORDER 2U
#define TRAIT_PANEL_CPU_COLUMNS 36U

enum trait_panel_status {
    TRAIT_PANEL_STATUS_OK = 0,
    TRAIT_PANEL_STATUS_NULL_ARGUMENT,
    TRAIT_PANEL_STATUS_NOT_INITIALIZED,
    TRAIT_PANEL_STATUS_BAD_INDEX,
    TRAIT_PANEL_STATUS_UNSUPPORTED_GEOMETRY,
    TRAIT_PANEL_STATUS_SURFACE_FAILURE,
    TRAIT_PANEL_STATUS_FONT_FAILURE
};

/* The plugins, in the order the profile lists them.  The order is the
 * identity of the bar, so it is an enumeration rather than a table
 * anybody can reshuffle. */
enum trait_panel_plugin {
    TRAIT_PANEL_PLUGIN_MENU = 0,
    TRAIT_PANEL_PLUGIN_LAUNCHBAR,
    TRAIT_PANEL_PLUGIN_WINCMD,
    TRAIT_PANEL_PLUGIN_PAGER,
    TRAIT_PANEL_PLUGIN_TASKBAR,
    TRAIT_PANEL_PLUGIN_CPU,
    TRAIT_PANEL_PLUGIN_VOLUME,
    TRAIT_PANEL_PLUGIN_TRAY,
    TRAIT_PANEL_PLUGIN_CLOCK,
    TRAIT_PANEL_PLUGIN_LAUNCHBAR_RIGHT,
    TRAIT_PANEL_PLUGIN_COUNT
};

struct trait_panel_task {
    char label[TRAIT_PANEL_LABEL_BYTES];
    const char *icon;      /* a name in trait_panel_art[] */
    bool active;
    bool minimised;
    uint32_t desktop;
};

/* The surface to draw on.  Handed in rather than fetched, so the same
 * panel serves the framebuffer and the preview harness. */
enum trait_panel_status trait_panel_attach(struct trait_surface *surface);
enum trait_panel_status trait_panel_initialize(void);
bool trait_panel_is_initialized(void);

enum trait_panel_status trait_panel_set_task(
    uint32_t slot, const struct trait_panel_task *task);
enum trait_panel_status trait_panel_clear_task(uint32_t slot);
uint32_t trait_panel_task_count(void);

/* One sample per call, 0..100, oldest dropped - which is what cpu.c's
 * ring does once a second. */
enum trait_panel_status trait_panel_push_cpu(uint32_t percent);

enum trait_panel_status trait_panel_set_clock(const char *text);
enum trait_panel_status trait_panel_set_volume(uint32_t level, bool muted);
enum trait_panel_status trait_panel_set_desktop(uint32_t current,
    uint32_t count);

/* Where the bar sits on a screen of this size, and where each plugin sits
 * inside it.  Published so a check can measure the layout rather than
 * trust it. */
struct trait_rect trait_panel_bounds(struct trait_rect screen);
enum trait_panel_status trait_panel_plugin_bounds(
    struct trait_rect screen, enum trait_panel_plugin which,
    struct trait_rect *out);

/*
 * WHAT A PRESS ON THE BAR LANDED ON.
 *
 * The panel draws launchers, a pager and a task button per window.  Until
 * this existed every one of them was a picture: drawn as a control and
 * doing nothing when pressed, which is the one thing this desktop is
 * built not to do.  The bar does not know what a launcher should LAUNCH -
 * it reports what was hit and the shell decides.
 */
enum trait_panel_hit_kind {
    TRAIT_PANEL_HIT_NONE = 0,
    TRAIT_PANEL_HIT_MENU,
    TRAIT_PANEL_HIT_LAUNCHER,   /* index: which launcher */
    TRAIT_PANEL_HIT_WINCMD,
    TRAIT_PANEL_HIT_PAGER,      /* index: which desktop */
    TRAIT_PANEL_HIT_TASK,       /* index: which task slot */
    TRAIT_PANEL_HIT_VOLUME,
    TRAIT_PANEL_HIT_CLOCK
};

struct trait_panel_hit {
    enum trait_panel_hit_kind kind;
    uint32_t index;
};

struct trait_panel_hit trait_panel_hit(struct trait_rect screen,
    uint32_t x, uint32_t y);

enum trait_panel_status trait_panel_draw(struct trait_rect screen);

const char *trait_panel_status_string(enum trait_panel_status status);
bool trait_panel_self_test(void);

#endif /* TRAIT_DE_PANEL_H */
