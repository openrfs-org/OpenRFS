/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DE_WINDOW_H
#define OPENRFS_DE_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include <openrfs/de/surface.h>

/*
 * An Openbox frame: a title bar with the window's name and its three
 * buttons, and a one-pixel border round the client area.
 *
 * The frame owns nothing inside it.  openrfs_window_client() says where the
 * application may draw and the application draws there; that is the whole
 * contract, and it is why the Task Manager and Settings below can be
 * written without either of them knowing what a title bar looks like.
 */

#define OPENRFS_TITLE_HEIGHT 22U
#define OPENRFS_BORDER 1U
#define OPENRFS_TITLE_BYTES 48U

struct openrfs_window {
    char title[OPENRFS_TITLE_BYTES];
    struct openrfs_rect frame;
    bool active;
    bool minimised;
    bool maximised;
    /* Which workspace it is on.  The pager switches which one you are
     * looking at; this is what makes that mean something. */
    uint32_t desktop;
    /* Where it was before it was maximised, so unmaximising puts it
     * back rather than guessing a size. */
    struct openrfs_rect restore;
};

/* The three title-bar buttons, as boxes, so the thing that is drawn and
 * the thing that answers a press are one definition. */
enum openrfs_window_button {
    OPENRFS_WINDOW_MINIMISE = 0,
    OPENRFS_WINDOW_MAXIMISE,
    OPENRFS_WINDOW_CLOSE
};

bool openrfs_window_button_bounds(const struct openrfs_window *window,
    enum openrfs_window_button which, struct openrfs_rect *out);

struct openrfs_rect openrfs_window_client(const struct openrfs_window *window);
struct openrfs_rect openrfs_window_title(const struct openrfs_window *window);
void openrfs_window_draw(struct openrfs_surface *surface,
    const struct openrfs_window *window);
void openrfs_window_set_title(struct openrfs_window *window, const char *text);

#endif /* OPENRFS_DE_WINDOW_H */
