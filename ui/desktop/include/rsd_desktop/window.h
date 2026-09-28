/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_WINDOW_H
#define RSD_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd_desktop/surface.h>

/*
 * AN OXYGEN FRAME, at KDE 4's own sizes.
 *
 * WHAT THIS IS NOT ANY MORE. It was an fvwm frame - a seven-pixel border
 * you could grab, a twenty-pixel bar, a bevel round every edge and Motif
 * marks in square buttons - and the sizes below were xenocara's
 * system.fvwmrc. The note that used to be here, kept because it explains
 * what the numbers were before and why:
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
 * The frame owns nothing inside it.  rsd_window_client() says where the
 * application may draw and the application draws there; that is the whole
 * contract, and it is why the Task Manager and Settings below can be
 * written without either of them knowing what a title bar looks like.
 */

/*
 * 24: the tallest face this desktop rasterised is 17 pixels on a line,
 * and Oxygen puts a margin above the caption and below it
 * (Metrics::TitleBar_TopMargin, TitleBar_BottomMargin). The bar is sized
 * to the face rather than the other way round, which is what
 * Decoration::recalculateBorders() does with QFontMetrics.
 */
#define RSD_TITLE_HEIGHT 24U

/*
 * 4: Oxygen's BorderNormal, which is smallSpacing * 2 -
 * Decoration::borderSize(). It was 7, which is fvwm's, and seven pixels
 * of grey round every window is most of why the old frame read as an X
 * session of 1995: a frame of that era was something you GRABBED and so
 * it was drawn at the size it could be grabbed at.
 */
#define RSD_BORDER 4U

/*
 * WHICH LEAVES THE GRAB. Four pixels is thin to hit, and KWin's answer
 * is not to draw a thicker border - it is to take the grab region INTO
 * the window past the border it drew. That is what this is: the frame
 * shows 4 and answers 8. The direction matters and only one of the two is
 * honest - a control may answer a press slightly outside what it draws,
 * but nothing here may be DRAWN as something it does not do.
 */
#define RSD_GRIP 8U

/* Oxygen's title-bar buttons: a round orb 18 pixels across with air
 * between them, right-aligned against the end of the bar. The marks
 * inside are laid out in the 21-unit space oxygenbutton.cpp draws them
 * in - see BUTTON_UNITS in window.c. */
#define RSD_BUTTON_SIDE 18U
#define RSD_BUTTON_GAP 3U
#define RSD_BUTTON_MARGIN 6U

/*
 * How much of each side belongs to the CORNER rather than to the side
 * bar.  fvwm draws a line across the border there, and it is not
 * decoration: inside it a drag resizes both dimensions at once, outside
 * it only one.  A line that does not mark a change in behaviour would be
 * a line this desktop does not draw.
 */
/* Oxygen rounds the top two corners of a decoration and leaves the
 * bottom square. The source's radii sit at 3.5, 2.5 and 4.0 px; a
 * framebuffer has no half, so this is the whole-pixel one. */
#define RSD_OXY_RADIUS 5U

#define RSD_CORNER 24U
#define RSD_TITLE_BYTES 48U

/* Which icon the panel puts on this window's task button. An index into
 * src/rsd_icons.h, held on the window because the panel has no idea
 * what kind of application a slot is running and should not have to
 * ask. */

struct rsd_window {
    char title[RSD_TITLE_BYTES];
    uint32_t icon;
    struct rsd_rect frame;
    bool active;
    bool minimised;
    bool maximised;
    /* Which workspace it is on.  The pager switches which one you are
     * looking at; this is what makes that mean something. */
    uint32_t desktop;
    /* Where it was before it was maximised, so unmaximising puts it
     * back rather than guessing a size. */
    struct rsd_rect restore;
};

/* The three title-bar buttons, as boxes, so the thing that is drawn and
 * the thing that answers a press are one definition. */
enum rsd_window_button {
    RSD_WINDOW_MINIMISE = 0,
    RSD_WINDOW_MAXIMISE,
    RSD_WINDOW_CLOSE
};

bool rsd_window_button_bounds(const struct rsd_window *window,
    enum rsd_window_button which, struct rsd_rect *out);

struct rsd_rect rsd_window_client(const struct rsd_window *window);
struct rsd_rect rsd_window_title(const struct rsd_window *window);
void rsd_window_draw(struct rsd_surface *surface,
    const struct rsd_window *window);
void rsd_window_set_title(struct rsd_window *window, const char *text);

#endif /* RSD_WINDOW_H */
