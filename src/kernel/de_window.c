/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/de/window.h>

#include <trait/de/font.h>
#include <trait/de/theme.h>

/* Openbox's title bar is a vertical ramp between two colours; this is
 * that ramp, one row at a time. */
static uint32_t ramp(uint32_t top, uint32_t bottom, uint32_t row,
    uint32_t height)
{
    uint32_t channel;
    uint32_t out = 0U;

    if (height <= 1U) {
        return top;
    }
    for (channel = 0U; channel < 3U; ++channel) {
        uint32_t shift = channel * 8U;
        uint32_t a = (top >> shift) & 0xFFU;
        uint32_t b = (bottom >> shift) & 0xFFU;

        out |= (((a * (height - 1U - row)) + (b * row)) /
                (height - 1U)) << shift;
    }
    return out;
}

struct trait_rect trait_window_title(const struct trait_window *window)
{
    struct trait_rect box = { 0U, 0U, 0U, 0U };

    if (window == NULL) {
        return box;
    }
    box.x = window->frame.x + TRAIT_BORDER;
    box.y = window->frame.y + TRAIT_BORDER;
    box.width = window->frame.width > TRAIT_BORDER * 2U ?
        window->frame.width - TRAIT_BORDER * 2U : 0U;
    box.height = TRAIT_TITLE_HEIGHT;
    return box;
}

struct trait_rect trait_window_client(const struct trait_window *window)
{
    struct trait_rect title = trait_window_title(window);
    struct trait_rect box = { 0U, 0U, 0U, 0U };
    uint32_t chrome = TRAIT_BORDER * 2U + TRAIT_TITLE_HEIGHT;

    if (window == NULL) {
        return box;
    }
    box.x = title.x;
    box.y = title.y + title.height;
    box.width = title.width;
    box.height = window->frame.height > chrome ?
        window->frame.height - chrome : 0U;
    return box;
}

void trait_window_set_title(struct trait_window *window, const char *text)
{
    uint32_t at = 0U;

    if (window == NULL) {
        return;
    }
    while (text != NULL && text[at] != '\0' &&
            at + 1U < TRAIT_TITLE_BYTES) {
        window->title[at] = text[at];
        ++at;
    }
    window->title[at] = '\0';
}

/*
 * The three buttons, as Openbox draws them at this size: a bar, a box and
 * a cross, in the title bar's own ink.  They are DRAWN rather than carried
 * as pictures because at eight pixels a picture is the same handful of
 * lines with a file around it.
 */
#define BUTTON_MARK 8U

/*
 * ONE DEFINITION of where each button is, used to draw it and to answer a
 * press on it.  Two definitions drift, and the way that shows up is a
 * close button that closes when you click slightly to the left of it.
 */
bool trait_window_button_bounds(const struct trait_window *window,
    enum trait_window_button which, struct trait_rect *out)
{
    struct trait_rect title = trait_window_title(window);
    uint32_t right;
    uint32_t step;

    if (window == NULL || out == NULL || title.width < 90U) {
        return false;
    }
    right = title.x + title.width;
    step = BUTTON_MARK + 8U;
    out->y = title.y + (title.height - BUTTON_MARK) / 2U;
    out->width = BUTTON_MARK;
    out->height = BUTTON_MARK;
    switch (which) {
    case TRAIT_WINDOW_CLOSE:
        out->x = right - 6U - BUTTON_MARK;
        return true;
    case TRAIT_WINDOW_MAXIMISE:
        out->x = right - 6U - BUTTON_MARK - step;
        return true;
    case TRAIT_WINDOW_MINIMISE:
        out->x = right - 6U - BUTTON_MARK - step * 2U;
        return true;
    default:
        return false;
    }
}

static void buttons(struct trait_surface *surface, struct trait_rect title,
    const struct trait_window *window, uint32_t ink)
{
    struct trait_rect box;
    uint32_t at;

    if (title.width < 90U) {
        return;
    }
    if (trait_window_button_bounds(window, TRAIT_WINDOW_CLOSE, &box)) {
        for (at = 0U; at < BUTTON_MARK; ++at) {
            trait_surface_plot(surface, title, box.x + at, box.y + at,
                               ink);
            trait_surface_plot(surface, title, box.x + at,
                box.y + BUTTON_MARK - 1U - at, ink);
        }
    }
    if (trait_window_button_bounds(window, TRAIT_WINDOW_MAXIMISE, &box)) {
        for (at = 0U; at < BUTTON_MARK; ++at) {
            trait_surface_plot(surface, title, box.x + at, box.y, ink);
            trait_surface_plot(surface, title, box.x + at,
                box.y + BUTTON_MARK - 1U, ink);
            trait_surface_plot(surface, title, box.x, box.y + at, ink);
            trait_surface_plot(surface, title,
                box.x + BUTTON_MARK - 1U, box.y + at, ink);
        }
        /* A MAXIMISED window's button shows the restore mark - two
         * offset boxes - because a button that looks the same in both
         * states does not say which one you are in. */
        if (window->maximised) {
            for (at = 0U; at < BUTTON_MARK - 3U; ++at) {
                trait_surface_plot(surface, title, box.x + 3U + at,
                                   box.y + 3U, ink);
                trait_surface_plot(surface, title, box.x + 3U,
                                   box.y + 3U + at, ink);
            }
        }
    }
    if (trait_window_button_bounds(window, TRAIT_WINDOW_MINIMISE, &box)) {
        for (at = 0U; at < BUTTON_MARK; ++at) {
            trait_surface_plot(surface, title, box.x + at,
                box.y + BUTTON_MARK - 1U, ink);
        }
    }
}

void trait_window_draw(struct trait_surface *surface,
    const struct trait_window *window)
{
    struct trait_rect title;
    struct trait_rect client;
    uint32_t top;
    uint32_t bottom;
    uint32_t ink;
    uint32_t row;
    uint32_t at;

    if (window == NULL || !trait_surface_valid(surface)) {
        return;
    }
    title = trait_window_title(window);
    client = trait_window_client(window);
    top = window->active ? TRAIT_FRAME_ACTIVE_TOP : TRAIT_FRAME_IDLE_TOP;
    bottom = window->active ?
        TRAIT_FRAME_ACTIVE_BOTTOM : TRAIT_FRAME_IDLE_BOTTOM;
    ink = window->active ? TRAIT_FRAME_INK : TRAIT_FRAME_INK_DIM;

    /* The border, drawn as the frame with the client punched out of it
     * afterwards - one fill rather than four strips. */
    trait_surface_fill(surface, window->frame, window->frame, bottom);

    for (row = 0U; row < title.height; ++row) {
        for (at = 0U; at < title.width; ++at) {
            trait_surface_plot(surface, title, title.x + at, title.y + row,
                               ramp(top, bottom, row, title.height));
        }
    }
    trait_font_draw(surface, title, title.x + 7U,
        title.y + title.height - 7U, window->title, ink);
    buttons(surface, title, window, ink);
    trait_surface_fill(surface, client, client, TRAIT_BG);
}
