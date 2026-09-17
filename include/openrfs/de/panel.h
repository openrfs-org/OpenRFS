/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DE_PANEL_H
#define OPENRFS_DE_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/de/surface.h>

/*
 * THE OPENRFS PANEL: lxpanel's bar, in C.
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
 * openrfs_panel_set_task(); whatever composites it decides what that
 * means.
 */

#define OPENRFS_PANEL_HEIGHT 26U       /* Global { height=26 } */
#define OPENRFS_PANEL_MAX_TASKS 12U
#define OPENRFS_PANEL_LABEL_BYTES 32U
#define OPENRFS_PANEL_MAX_DESKTOPS 4U

/* taskbar { MaxTaskWidth=150 } in the profile's own plugin block. */
#define OPENRFS_PANEL_MAX_TASK_WIDTH 150U

/* cpu.c: the widget is 40 wide and BORDER_SIZE is 2, so the graph inside
 * it is 36 columns by 22 rows and each column is one sample. */
#define OPENRFS_PANEL_CPU_WIDTH 40U
#define OPENRFS_PANEL_CPU_BORDER 2U
#define OPENRFS_PANEL_CPU_COLUMNS 36U

enum openrfs_panel_status {
    OPENRFS_PANEL_STATUS_OK = 0,
    OPENRFS_PANEL_STATUS_NULL_ARGUMENT,
    OPENRFS_PANEL_STATUS_NOT_INITIALIZED,
    OPENRFS_PANEL_STATUS_BAD_INDEX,
    OPENRFS_PANEL_STATUS_UNSUPPORTED_GEOMETRY,
    OPENRFS_PANEL_STATUS_SURFACE_FAILURE,
    OPENRFS_PANEL_STATUS_FONT_FAILURE
};

/* The plugins, in the order the profile lists them.  The order is the
 * identity of the bar, so it is an enumeration rather than a table
 * anybody can reshuffle. */
enum openrfs_panel_plugin {
    OPENRFS_PANEL_PLUGIN_MENU = 0,
    OPENRFS_PANEL_PLUGIN_LAUNCHBAR,
    OPENRFS_PANEL_PLUGIN_WINCMD,
    OPENRFS_PANEL_PLUGIN_PAGER,
    OPENRFS_PANEL_PLUGIN_TASKBAR,
    OPENRFS_PANEL_PLUGIN_CPU,
    OPENRFS_PANEL_PLUGIN_VOLUME,
    OPENRFS_PANEL_PLUGIN_TRAY,
    OPENRFS_PANEL_PLUGIN_CLOCK,
    OPENRFS_PANEL_PLUGIN_LAUNCHBAR_RIGHT,
    OPENRFS_PANEL_PLUGIN_COUNT
};

struct openrfs_panel_task {
    char label[OPENRFS_PANEL_LABEL_BYTES];
    const char *icon;      /* a name in openrfs_panel_art[] */
    bool active;
    bool minimised;
    uint32_t desktop;
};

/* The surface to draw on.  Handed in rather than fetched, so the same
 * panel serves the framebuffer and the preview harness. */
enum openrfs_panel_status openrfs_panel_attach(struct openrfs_surface *surface);
enum openrfs_panel_status openrfs_panel_initialize(void);
bool openrfs_panel_is_initialized(void);

enum openrfs_panel_status openrfs_panel_set_task(
    uint32_t slot, const struct openrfs_panel_task *task);
enum openrfs_panel_status openrfs_panel_clear_task(uint32_t slot);
uint32_t openrfs_panel_task_count(void);

/* One sample per call, 0..100, oldest dropped - which is what cpu.c's
 * ring does once a second. */
enum openrfs_panel_status openrfs_panel_push_cpu(uint32_t percent);

enum openrfs_panel_status openrfs_panel_set_clock(const char *text);
enum openrfs_panel_status openrfs_panel_set_volume(uint32_t level, bool muted);
enum openrfs_panel_status openrfs_panel_set_desktop(uint32_t current,
    uint32_t count);

/* Where the bar sits on a screen of this size, and where each plugin sits
 * inside it.  Published so a check can measure the layout rather than
 * trust it. */
struct openrfs_rect openrfs_panel_bounds(struct openrfs_rect screen);
enum openrfs_panel_status openrfs_panel_plugin_bounds(
    struct openrfs_rect screen, enum openrfs_panel_plugin which,
    struct openrfs_rect *out);

/*
 * WHAT A PRESS ON THE BAR LANDED ON.
 *
 * The panel draws launchers, a pager and a task button per window.  Until
 * this existed every one of them was a picture: drawn as a control and
 * doing nothing when pressed, which is the one thing this desktop is
 * built not to do.  The bar does not know what a launcher should LAUNCH -
 * it reports what was hit and the shell decides.
 */
enum openrfs_panel_hit_kind {
    OPENRFS_PANEL_HIT_NONE = 0,
    OPENRFS_PANEL_HIT_MENU,
    OPENRFS_PANEL_HIT_LAUNCHER,   /* index: which launcher */
    OPENRFS_PANEL_HIT_WINCMD,
    OPENRFS_PANEL_HIT_PAGER,      /* index: which desktop */
    OPENRFS_PANEL_HIT_TASK,       /* index: which task slot */
    OPENRFS_PANEL_HIT_VOLUME,
    OPENRFS_PANEL_HIT_CLOCK
};

struct openrfs_panel_hit {
    enum openrfs_panel_hit_kind kind;
    uint32_t index;
};

struct openrfs_panel_hit openrfs_panel_hit(struct openrfs_rect screen,
    uint32_t x, uint32_t y);

enum openrfs_panel_status openrfs_panel_draw(struct openrfs_rect screen);

const char *openrfs_panel_status_string(enum openrfs_panel_status status);
bool openrfs_panel_self_test(void);

#endif /* OPENRFS_DE_PANEL_H */
