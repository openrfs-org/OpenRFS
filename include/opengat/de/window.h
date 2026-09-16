/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_DE_WINDOW_H
#define OPENGAT_DE_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include <opengat/de/surface.h>

/*
 * An Openbox frame: a title bar with the window's name and its three
 * buttons, and a one-pixel border round the client area.
 *
 * The frame owns nothing inside it.  opengat_window_client() says where the
 * application may draw and the application draws there; that is the whole
 * contract, and it is why the Task Manager and Settings below can be
 * written without either of them knowing what a title bar looks like.
 */

#define OPENGAT_TITLE_HEIGHT 22U
#define OPENGAT_BORDER 1U
#define OPENGAT_TITLE_BYTES 48U

struct opengat_window {
    char title[OPENGAT_TITLE_BYTES];
    struct opengat_rect frame;
    bool active;
    bool minimised;
    bool maximised;
    /* Which workspace it is on.  The pager switches which one you are
     * looking at; this is what makes that mean something. */
    uint32_t desktop;
    /* Where it was before it was maximised, so unmaximising puts it
     * back rather than guessing a size. */
    struct opengat_rect restore;
};

/* The three title-bar buttons, as boxes, so the thing that is drawn and
 * the thing that answers a press are one definition. */
enum opengat_window_button {
    OPENGAT_WINDOW_MINIMISE = 0,
    OPENGAT_WINDOW_MAXIMISE,
    OPENGAT_WINDOW_CLOSE
};

bool opengat_window_button_bounds(const struct opengat_window *window,
    enum opengat_window_button which, struct opengat_rect *out);

struct opengat_rect opengat_window_client(const struct opengat_window *window);
struct opengat_rect opengat_window_title(const struct opengat_window *window);
void opengat_window_draw(struct opengat_surface *surface,
    const struct opengat_window *window);
void opengat_window_set_title(struct opengat_window *window, const char *text);

#endif /* OPENGAT_DE_WINDOW_H */
