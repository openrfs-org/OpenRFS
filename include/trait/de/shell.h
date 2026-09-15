/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_DE_SHELL_H
#define TRAIT_DE_SHELL_H

#include <stdbool.h>
#include <stdint.h>

#include <trait/de/input.h>
#include <trait/de/surface.h>
#include <trait/de/window.h>

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

#define TRAIT_SHELL_MAX_WINDOWS 8U

/*
 * The pid a window's Task Manager row carries, for slot 0.  It is 2 and
 * not 1 because pid 1 is the session, which refuses to be ended - a
 * window that happened to be opened first should not inherit that.
 */
#define TRAIT_SHELL_FIRST_PID 2U

enum trait_shell_app {
    TRAIT_APP_FILES = 0,
    TRAIT_APP_TERMINAL,
    TRAIT_APP_TASKMGR,
    TRAIT_APP_SETTINGS,
    TRAIT_APP_PACKAGES,
    TRAIT_APP_COUNT
};

void trait_shell_reset(struct trait_surface *surface);

/*
 * The screen the shell lays out against.  Taken from the surface by
 * trait_shell_reset(), and settable on its own because the two are not
 * the same thing: a surface is where pixels go, a screen is what
 * "maximised" means.  The self-test needs the second without the first.
 */
void trait_shell_set_screen(struct trait_rect screen);

/* Which workspace is showing.  Windows on the others are not drawn and
 * not hit, which is what a workspace IS. */
void trait_shell_set_desktop(uint32_t desktop);
uint32_t trait_shell_desktop(void);
void trait_shell_send_to_desktop(uint32_t slot, uint32_t desktop);

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
void trait_shell_set_desktop_folder(uint32_t folder);
void trait_shell_draw_desktop(void);
bool trait_shell_desktop_icon_bounds(uint32_t at, struct trait_rect *out);
uint32_t trait_shell_desktop_icon_count(void);

/* The Run box: type a name, press return, and it runs or says it cannot. */
/*
 * pcmanfm's CONTEXT MENU, on a file-manager entry.  It is shell state
 * rather than file-manager state for the same reason the applications
 * menu is: it is an overlay that sits above every window, and the shell
 * is what knows there are windows to sit above.
 */
bool trait_shell_context_open(void);
struct trait_rect trait_shell_context_bounds(void);
uint32_t trait_shell_context_row_count(void);
const char *trait_shell_context_row(uint32_t at);

/* The rename box that Rename opens: a real field, and it refuses the
 * names trait_files_rename() refuses, out loud. */
bool trait_shell_rename_open(void);
uint32_t trait_shell_context_node(void);
const char *trait_shell_rename_text(void);
const char *trait_shell_rename_error(void);

bool trait_shell_run_open(void);
const char *trait_shell_run_text(void);
const char *trait_shell_run_error(void);

/* Alt+Tab.  Held open while Alt is down, which is why it is state. */
bool trait_shell_switcher_open(void);
uint32_t trait_shell_switcher_at(void);

/*
 * NOTIFICATIONS.  Something happened that the user did not watch happen -
 * a package applied, a file moved - and the desktop says so.  A queue
 * rather than one slot, because two things can happen at once and the
 * second one silently replacing the first is worse than no notice.
 */
#define TRAIT_SHELL_MAX_NOTES 3U
#define TRAIT_SHELL_NOTE_BYTES 64U

void trait_shell_notify(const char *title, const char *body);
uint32_t trait_shell_note_count(void);
const char *trait_shell_note_title(uint32_t at);
const char *trait_shell_note_body(uint32_t at);
/* One tick of the clock: notices age out on their own. */
void trait_shell_tick(void);

/*
 * TOOLTIPS.  A pointer resting on a bar button says what it is.  The
 * DELAY is the whole of what makes a tip helpful rather than a thing that
 * flashes at you while you move the mouse across the screen - GTK's own
 * is 500ms, and this counts ticks rather than guessing.
 */
#define TRAIT_SHELL_TIP_TICKS 5U
bool trait_shell_tip_visible(void);
const char *trait_shell_tip_text(void);
struct trait_rect trait_shell_tip_bounds(void);

bool trait_shell_menu_open(void);
bool trait_shell_volume_open(void);
uint32_t trait_shell_volume(void);
struct trait_rect trait_shell_screen(void);

/* Returns the slot, or TRAIT_SHELL_MAX_WINDOWS if there is no room. */
uint32_t trait_shell_open(enum trait_shell_app app, struct trait_rect at);
bool trait_shell_close(uint32_t slot);
uint32_t trait_shell_window_count(void);
struct trait_window *trait_shell_window(uint32_t slot);
enum trait_shell_app trait_shell_app_of(uint32_t slot);

/* The window under a point, topmost first, or TRAIT_SHELL_MAX_WINDOWS. */
uint32_t trait_shell_at(uint32_t x, uint32_t y);
uint32_t trait_shell_focused(void);
void trait_shell_focus(uint32_t slot);

/* Returns true if the event changed anything, so a caller can redraw
 * only when it must. */
bool trait_shell_handle(const struct trait_event *event);

void trait_shell_draw(void);
/* Call AFTER the panel: an open menu or slider sits above everything,
 * including the bar that opened it. */
void trait_shell_draw_overlays(void);

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
typedef bool (*trait_event_source)(struct trait_event *out, void *context);
typedef void (*trait_present_fn)(void *context);

uint32_t trait_shell_run(trait_event_source next, trait_present_fn redraw,
    void *context);

bool trait_shell_self_test(void);

#endif /* TRAIT_DE_SHELL_H */
