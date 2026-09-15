/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_DE_WINDOW_H
#define TRAIT_DE_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include <trait/de/surface.h>

/*
 * An Openbox frame: a title bar with the window's name and its three
 * buttons, and a one-pixel border round the client area.
 *
 * The frame owns nothing inside it.  trait_window_client() says where the
 * application may draw and the application draws there; that is the whole
 * contract, and it is why the Task Manager and Settings below can be
 * written without either of them knowing what a title bar looks like.
 */

#define TRAIT_TITLE_HEIGHT 22U
#define TRAIT_BORDER 1U
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

#endif /* TRAIT_DE_WINDOW_H */
