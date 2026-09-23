/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_SHELL_H
#define TRAIT_SHELL_H

#include <stdbool.h>
#include <stdint.h>

#include <trait/input.h>
#include <trait/surface.h>
#include <trait/window.h>

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
    /* glxgears, which is a PROGRAM.  It ran as the root window for a
     * while, which is not where it runs: you type glxgears and a window
     * comes up with the gears turning in it. */
    TRAIT_APP_GEARS,
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
/* pcmanfm draws the desktop, so turning its icons off is turning the
 * desktop's own drawing off - what is behind them stays. */
/*
 * THE ROOT WINDOW, which is glxgears.
 *
 * Not a picture of it: trait_gears_draw() fills Brian Paul's gears from
 * their own radii and tooth counts, so the root has no resolution and
 * no image file behind it, and it comes out right on a screen no one
 * has measured yet.
 */
void trait_shell_draw_root(void);

/*
 * THE ROOT MENU, at the point that was pressed, and there is no other.
 *
 * There is no bar, no dock and no tray: a window manager of this kind
 * has none, and every one of them was a place where a control could sit
 * and do nothing.  You start something from the root menu, you reach a
 * window by clicking it or with Alt+Tab, and what is running is in the
 * Task Manager.  Returns false if the press was not on the root.
 */
bool trait_shell_root_press(uint32_t x, uint32_t y);
bool trait_shell_root_menu_open(void);
bool trait_shell_root_menu_bounds(struct trait_rect *out);

void trait_shell_set_desktop_icons(bool show);
bool trait_shell_desktop_icons(void);
void trait_shell_draw_desktop(void);
bool trait_shell_desktop_icon_bounds(uint32_t at, struct trait_rect *out);

/*
 * ICONIFIED WINDOWS, ON THE ROOT, which is where a minimised window
 * goes and the reason there is no taskbar to miss it from.  fvwm drops
 * an icon at the foot of the screen and OpenBSD comes up on fvwm; a
 * window that lives only in Alt+Tab is a window you have to remember
 * you have.  Pressing one puts it back.
 */
uint32_t trait_shell_window_icon_count(void);
uint32_t trait_shell_window_icon_slot(uint32_t at);
bool trait_shell_window_icon_bounds(uint32_t at, struct trait_rect *out);
void trait_shell_draw_window_icons(void);
uint32_t trait_shell_desktop_icon_count(void);

/*
 * THE LAUNCHER, which is dmenu's.
 *
 * A strip across the top of the screen: a prompt, what you have typed,
 * and the programs it matches laid out along the rest of it with one of
 * them selected.  Typing narrows the list, the arrow keys move along
 * it, Tab copies the selected name into the input, Return runs it and
 * Escape leaves.  The geometry is dmenu's own - see the note in
 * src/shell.c for which lines of dmenu.c each number is from.
 *
 * The box it replaces asked you for a name and could not tell you what
 * there was, so the only way to use it was to already know.
 */
bool trait_shell_run_bounds(struct trait_rect *out);
uint32_t trait_shell_run_match_count(void);
const char *trait_shell_run_match(uint32_t at);
uint32_t trait_shell_run_selected(void);

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
/* Call AFTER trait_shell_draw(): the root menu sits above every
 * window, so it cannot be drawn with the stack. */
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

#endif /* TRAIT_SHELL_H */
