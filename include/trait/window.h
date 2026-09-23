/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_WINDOW_H
#define TRAIT_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include <trait/surface.h>

/*
 * AN FVWM FRAME, at the sizes OpenBSD's own fvwm comes up with.
 *
 * It was an Openbox frame: a one-pixel border and a title bar drawn as a
 * vertical ramp.  OpenBSD ships fvwm and fvwm is where the classic X
 * desktop's shape comes from, so the numbers below are that shell's
 * rather than this one's - out of xenocara's app/fvwm/sample.fvwmrc/
 * system.fvwmrc, which is the file a fresh install reads:
 *
 *     Style "*"  BorderWidth 7, HandleWidth 7
 *     Style "*"  Color #bebebe/darkred
 *     HilightColor #bebebe blue
 *     WindowFont -adobe-times-bold-r-*-*-14-*
 *
 * Seven pixels of border is the whole difference between a frame you
 * look at and a frame you GRAB, which is why fvwm has that much and a
 * modern one has none: there is no invisible resize region here and
 * there was none there.  The border is what you drag, so the border is
 * drawn at the size it can be dragged at - see RESIZE_GRIP in shell.c,
 * which is this number.
 *
 * The frame owns nothing inside it.  trait_window_client() says where the
 * application may draw and the application draws there; that is the whole
 * contract, and it is why the Task Manager and Settings below can be
 * written without either of them knowing what a title bar looks like.
 */

/* 20: sixteen rows of Misc-Fixed and two either side.  fvwm's own is
 * Times bold 14 and it sizes the bar to the face it is given. */
#define TRAIT_TITLE_HEIGHT 20U
#define TRAIT_BORDER 7U

/* The bevel fvwm relieves every edge with. */
#define TRAIT_RELIEF_WIDTH 2U

/*
 * How much of each side belongs to the CORNER rather than to the side
 * bar.  fvwm draws a line across the border there, and it is not
 * decoration: inside it a drag resizes both dimensions at once, outside
 * it only one.  A line that does not mark a change in behaviour would be
 * a line this desktop does not draw.
 */
#define TRAIT_CORNER 24U
#define TRAIT_TITLE_BYTES 48U

struct trait_window {
    char title[TRAIT_TITLE_BYTES];
    struct trait_rect frame;
    bool active;
    bool minimised;
    bool maximised;
    /* Which workspace it is on.  The pager switches which one you are
     * looking at; this is what makes that mean something. */
    uint32_t desktop;
    /* Where it was before it was maximised, so unmaximising puts it
     * back rather than guessing a size. */
    struct trait_rect restore;
};

/* The three title-bar buttons, as boxes, so the thing that is drawn and
 * the thing that answers a press are one definition. */
enum trait_window_button {
    TRAIT_WINDOW_MINIMISE = 0,
    TRAIT_WINDOW_MAXIMISE,
    TRAIT_WINDOW_CLOSE
};

bool trait_window_button_bounds(const struct trait_window *window,
    enum trait_window_button which, struct trait_rect *out);

struct trait_rect trait_window_client(const struct trait_window *window);
struct trait_rect trait_window_title(const struct trait_window *window);
void trait_window_draw(struct trait_surface *surface,
    const struct trait_window *window);
void trait_window_set_title(struct trait_window *window, const char *text);

#endif /* TRAIT_WINDOW_H */
