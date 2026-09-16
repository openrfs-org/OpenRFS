/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_DE_SHELL_H
#define OPENGAT_DE_SHELL_H

#include <stdbool.h>
#include <stdint.h>

#include <opengat/de/input.h>
#include <opengat/de/surface.h>
#include <opengat/de/window.h>

/*
 * THE SHELL: what owns the windows and routes the events.
 *
 * Every module below it draws and models; none of them knows another
 * exists.  This is the one place that knows there is more than one
 * window, which is why it is the one place that can say what a click on
 * a given pixel means.
 *
 * The rule it enforces is the one the whole desktop is built on: a click
 * lands on the TOPMOST thing under it, and that thing does what it is
 * drawn as.  A control that is covered does not receive the click that
 * looks like it landed on the thing above it.
 */

#define OPENGAT_SHELL_MAX_WINDOWS 8U

/*
 * The pid a window's Task Manager row carries, for slot 0.  It is 2 and
 * not 1 because pid 1 is the session, which refuses to be ended - a
 * window that happened to be opened first should not inherit that.
 */
#define OPENGAT_SHELL_FIRST_PID 2U

enum opengat_shell_app {
    OPENGAT_APP_FILES = 0,
    OPENGAT_APP_TERMINAL,
    OPENGAT_APP_TASKMGR,
    OPENGAT_APP_SETTINGS,
    OPENGAT_APP_PACKAGES,
    OPENGAT_APP_COUNT
};

void opengat_shell_reset(struct opengat_surface *surface);

/*
 * The screen the shell lays out against.  Taken from the surface by
 * opengat_shell_reset(), and settable on its own because the two are not
 * the same thing: a surface is where pixels go, a screen is what
 * "maximised" means.  The self-test needs the second without the first.
 */
void opengat_shell_set_screen(struct opengat_rect screen);

/* Which workspace is showing.  Windows on the others are not drawn and
 * not hit, which is what a workspace IS. */
void opengat_shell_set_desktop(uint32_t desktop);
uint32_t opengat_shell_desktop(void);
void opengat_shell_send_to_desktop(uint32_t slot, uint32_t desktop);

/* The two things the bar can open.  They are shell state rather than
 * panel state because both are overlays that sit above every window, and
 * the shell is what knows there are windows to sit above. */
/*
 * THE ROOT WINDOW.
 *
 * pcmanfm's desktop IS ~/Desktop drawn on the root window, plus the two
 * standard marks.  Drawing a fixed pair and calling it the desktop makes
 * "Create New..." and Paste into decorations, so this reads the folder.
 */
/* Which folder the root window draws.  Handed in, so the shell does not
 * have to know what ~/Desktop's node index happens to be. */
void opengat_shell_set_desktop_folder(uint32_t folder);
void opengat_shell_draw_desktop(void);
bool opengat_shell_desktop_icon_bounds(uint32_t at, struct opengat_rect *out);
uint32_t opengat_shell_desktop_icon_count(void);

/* The Run box: type a name, press return, and it runs or says it cannot. */
/*
 * pcmanfm's CONTEXT MENU, on a file-manager entry.  It is shell state
 * rather than file-manager state for the same reason the applications
 * menu is: it is an overlay that sits above every window, and the shell
 * is what knows there are windows to sit above.
 */
bool opengat_shell_context_open(void);
struct opengat_rect opengat_shell_context_bounds(void);
uint32_t opengat_shell_context_row_count(void);
const char *opengat_shell_context_row(uint32_t at);

/* The rename box that Rename opens: a real field, and it refuses the
 * names opengat_files_rename() refuses, out loud. */
bool opengat_shell_rename_open(void);
uint32_t opengat_shell_context_node(void);
const char *opengat_shell_rename_text(void);
const char *opengat_shell_rename_error(void);

bool opengat_shell_run_open(void);
const char *opengat_shell_run_text(void);
const char *opengat_shell_run_error(void);

/* Alt+Tab.  Held open while Alt is down, which is why it is state. */
bool opengat_shell_switcher_open(void);
uint32_t opengat_shell_switcher_at(void);

/*
 * NOTIFICATIONS.  Something happened that the user did not watch happen -
 * a package applied, a file moved - and the desktop says so.  A queue
 * rather than one slot, because two things can happen at once and the
 * second one silently replacing the first is worse than no notice.
 */
#define OPENGAT_SHELL_MAX_NOTES 3U
#define OPENGAT_SHELL_NOTE_BYTES 64U

void opengat_shell_notify(const char *title, const char *body);
uint32_t opengat_shell_note_count(void);
const char *opengat_shell_note_title(uint32_t at);
const char *opengat_shell_note_body(uint32_t at);
/* One tick of the clock: notices age out on their own. */
void opengat_shell_tick(void);

/*
 * TOOLTIPS.  A pointer resting on a bar button says what it is.  The
 * DELAY is the whole of what makes a tip helpful rather than a thing that
 * flashes at you while you move the mouse across the screen - GTK's own
 * is 500ms, and this counts ticks rather than guessing.
 */
#define OPENGAT_SHELL_TIP_TICKS 5U
bool opengat_shell_tip_visible(void);
const char *opengat_shell_tip_text(void);
struct opengat_rect opengat_shell_tip_bounds(void);

bool opengat_shell_menu_open(void);
bool opengat_shell_volume_open(void);
uint32_t opengat_shell_volume(void);
struct opengat_rect opengat_shell_screen(void);

/* Returns the slot, or OPENGAT_SHELL_MAX_WINDOWS if there is no room. */
uint32_t opengat_shell_open(enum opengat_shell_app app, struct opengat_rect at);
bool opengat_shell_close(uint32_t slot);
uint32_t opengat_shell_window_count(void);
struct opengat_window *opengat_shell_window(uint32_t slot);
enum opengat_shell_app opengat_shell_app_of(uint32_t slot);

/* The window under a point, topmost first, or OPENGAT_SHELL_MAX_WINDOWS. */
uint32_t opengat_shell_at(uint32_t x, uint32_t y);
uint32_t opengat_shell_focused(void);
void opengat_shell_focus(uint32_t slot);

/* Returns true if the event changed anything, so a caller can redraw
 * only when it must. */
bool opengat_shell_handle(const struct opengat_event *event);

void opengat_shell_draw(void);
/* Call AFTER the panel: an open menu or slider sits above everything,
 * including the bar that opened it. */
void opengat_shell_draw_overlays(void);

/*
 * THE MAIN LOOP.
 *
 * The event source is a CALLBACK rather than a device, which is what lets
 * the same loop serve a real keyboard and mouse on the metal and a
 * scripted list in the harness: the loop does not know where events come
 * from, and neither of those two has to be compiled into the other.
 *
 * Fill `out` and return true for another event; return false to stop.
 * `redraw` is called once after any event that CHANGED something, not
 * once per event - a desktop that repaints on every mouse move is a
 * desktop that does nothing else.
 */
typedef bool (*opengat_event_source)(struct opengat_event *out, void *context);
typedef void (*opengat_present_fn)(void *context);

uint32_t opengat_shell_run(opengat_event_source next, opengat_present_fn redraw,
    void *context);

bool opengat_shell_self_test(void);

#endif /* OPENGAT_DE_SHELL_H */
