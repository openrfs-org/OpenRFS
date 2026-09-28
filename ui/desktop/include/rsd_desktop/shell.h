/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_DESKTOP_SHELL_H
#define RSD_DESKTOP_SHELL_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd_desktop/input.h>
#include <rsd_desktop/surface.h>
#include <rsd_desktop/window.h>

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

#define RSD_SHELL_MAX_WINDOWS 8U

/*
 * The pid a window's Task Manager row carries, for slot 0.  It is 2 and
 * not 1 because pid 1 is the session, which refuses to be ended - a
 * window that happened to be opened first should not inherit that.
 */
#define RSD_SHELL_FIRST_PID 2U

enum rsd_shell_app {
    RSD_APP_FILES = 0,
    RSD_APP_TERMINAL,
    RSD_APP_TASKMGR,
    RSD_APP_SETTINGS,
    RSD_APP_PACKAGES,
    RSD_APP_COUNT
};

void rsd_shell_reset(struct rsd_surface *surface);

/*
 * The screen the shell lays out against.  Taken from the surface by
 * rsd_shell_reset(), and settable on its own because the two are not
 * the same thing: a surface is where pixels go, a screen is what
 * "maximised" means.  The self-test needs the second without the first.
 */
void rsd_shell_set_screen(struct rsd_rect screen);

/* Which workspace is showing.  Windows on the others are not drawn and
 * not hit, which is what a workspace IS. */
void rsd_shell_set_desktop(uint32_t desktop);
uint32_t rsd_shell_desktop(void);
void rsd_shell_send_to_desktop(uint32_t slot, uint32_t desktop);

/*
 * THE ROOT WINDOW.
 *
 * pcmanfm's desktop IS ~/Desktop drawn on the root window, plus the two
 * standard marks.  Drawing a fixed pair and calling it the desktop makes
 * "Create New..." and Paste into decorations, so this reads the folder.
 */
/* Which folder the root window draws.  Handed in, so the shell does not
 * have to know what ~/Desktop's node index happens to be. */
void rsd_shell_set_desktop_folder(uint32_t folder);
/* pcmanfm draws the desktop, so turning its icons off is turning the
 * desktop's own drawing off - what is behind them stays. */
/*
 * THE ROOT WINDOW, which is the wallpaper.
 *
 * It was glxgears for a while - the gears drawn from geometry, filling
 * the whole screen with nothing over them, which is a thing people
 * genuinely used to do. The desktop has a picture on it now and a panel
 * along the bottom, and a screen full of turning gears under both is a
 * joke that has stopped being funny. rsd_space_draw() blits the
 * owner's wallpaper out of a table instead.
 */
void rsd_shell_draw_root(void);

/*
 * THE ROOT MENU, at the point that was pressed, and there is no other.
 *
 * There is no bar, no dock and no tray: a window manager of this kind
 * has none, and every one of them was a place where a control could sit
 * and do nothing.  You start something from the root menu, you reach a
 * window by clicking it or with Alt+Tab, and what is running is in the
 * Task Manager.  Returns false if the press was not on the root.
 */
bool rsd_shell_root_press(uint32_t x, uint32_t y);
bool rsd_shell_root_menu_open(void);
bool rsd_shell_root_menu_bounds(struct rsd_rect *out);

void rsd_shell_set_desktop_icons(bool show);
bool rsd_shell_desktop_icons(void);
void rsd_shell_draw_desktop(void);
bool rsd_shell_desktop_icon_bounds(uint32_t at, struct rsd_rect *out);

/*
 * ICONIFIED WINDOWS, ON THE ROOT, which is where a minimised window
 * goes and the reason there is no taskbar to miss it from.  fvwm drops
 * an icon at the foot of the screen and OpenBSD comes up on fvwm; a
 * window that lives only in Alt+Tab is a window you have to remember
 * you have.  Pressing one puts it back.
 */
uint32_t rsd_shell_window_icon_count(void);
uint32_t rsd_shell_window_icon_slot(uint32_t at);
bool rsd_shell_window_icon_bounds(uint32_t at, struct rsd_rect *out);
void rsd_shell_draw_window_icons(void);
uint32_t rsd_shell_desktop_icon_count(void);

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
bool rsd_shell_run_bounds(struct rsd_rect *out);
uint32_t rsd_shell_run_match_count(void);
const char *rsd_shell_run_match(uint32_t at);
uint32_t rsd_shell_run_selected(void);

/* The Run box: type a name, press return, and it runs or says it cannot. */
/*
 * pcmanfm's CONTEXT MENU, on a file-manager entry.  It is shell state
 * rather than file-manager state for the same reason the applications
 * menu is: it is an overlay that sits above every window, and the shell
 * is what knows there are windows to sit above.
 */
bool rsd_shell_context_open(void);
struct rsd_rect rsd_shell_context_bounds(void);
uint32_t rsd_shell_context_row_count(void);
const char *rsd_shell_context_row(uint32_t at);

/* The rename box that Rename opens: a real field, and it refuses the
 * names rsd_files_rename() refuses, out loud. */
bool rsd_shell_rename_open(void);
uint32_t rsd_shell_context_node(void);
const char *rsd_shell_rename_text(void);
const char *rsd_shell_rename_error(void);

bool rsd_shell_run_open(void);
const char *rsd_shell_run_text(void);
const char *rsd_shell_run_error(void);

/* Alt+Tab.  Held open while Alt is down, which is why it is state. */
bool rsd_shell_switcher_open(void);
uint32_t rsd_shell_switcher_at(void);

/*
 * NOTIFICATIONS.  Something happened that the user did not watch happen -
 * a package applied, a file moved - and the desktop says so.  A queue
 * rather than one slot, because two things can happen at once and the
 * second one silently replacing the first is worse than no notice.
 */
#define RSD_SHELL_MAX_NOTES 3U
#define RSD_SHELL_NOTE_BYTES 64U

void rsd_shell_notify(const char *title, const char *body);
uint32_t rsd_shell_note_count(void);
const char *rsd_shell_note_title(uint32_t at);
const char *rsd_shell_note_body(uint32_t at);
/* One tick of the clock: notices age out on their own. */
void rsd_shell_tick(void);

struct rsd_rect rsd_shell_screen(void);

/* Returns the slot, or RSD_SHELL_MAX_WINDOWS if there is no room. */
uint32_t rsd_shell_open(enum rsd_shell_app app, struct rsd_rect at);
bool rsd_shell_close(uint32_t slot);
uint32_t rsd_shell_window_count(void);
struct rsd_window *rsd_shell_window(uint32_t slot);
enum rsd_shell_app rsd_shell_app_of(uint32_t slot);

/* The window under a point, topmost first, or RSD_SHELL_MAX_WINDOWS. */
uint32_t rsd_shell_at(uint32_t x, uint32_t y);
uint32_t rsd_shell_focused(void);
void rsd_shell_focus(uint32_t slot);

/* Returns true if the event changed anything, so a caller can redraw
 * only when it must. */
bool rsd_shell_handle(const struct rsd_event *event);

/* The wall clock, as the panel prints it. The platform owns the
 * time; this is how it hands it over. */
void rsd_shell_set_clock(const char *time, const char *date);

void rsd_shell_draw(void);
/* Call AFTER rsd_shell_draw(): the root menu sits above every
 * window, so it cannot be drawn with the stack. */
void rsd_shell_draw_overlays(void);

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
typedef bool (*rsd_event_source)(struct rsd_event *out, void *context);
typedef void (*rsd_present_fn)(void *context);

uint32_t rsd_shell_run(rsd_event_source next, rsd_present_fn redraw,
    void *context);

bool rsd_shell_self_test(void);

#endif /* RSD_DESKTOP_SHELL_H */
