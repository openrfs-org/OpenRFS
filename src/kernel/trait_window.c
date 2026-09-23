/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/window.h>

#include <trait/font.h>
#include <trait/theme.h>

/*
 * THE BEVEL, which is the whole of fvwm's third dimension.
 *
 * There is no gradient anywhere in an fvwm frame and no shadow under
 * anything.  What makes a border look like a bar you can grab is two
 * lines: the side the light falls on and the side it does not, drawn
 * `depth` pixels thick around a rectangle.  Swap the two and the same
 * code draws a hole instead of a bar, which is how the inside edge of
 * the border and the pressed state of a button are drawn.
 *
 * The two colours are never mixed or shaded here.  They arrive already
 * computed, out of src/trait_relief.h, by fvwm's own GetHilite() and
 * GetShadow().
 */
static void relieve(struct trait_surface *surface, struct trait_rect clip,
    struct trait_rect box, uint32_t lit, uint32_t shade, uint32_t depth)
{
    uint32_t ring;

    for (ring = 0U; ring < depth; ++ring) {
        uint32_t x;
        uint32_t y;

        if (box.width <= ring * 2U || box.height <= ring * 2U) {
            return;
        }
        for (x = box.x + ring; x < box.x + box.width - ring; ++x) {
            trait_surface_plot(surface, clip, x, box.y + ring, lit);
            trait_surface_plot(surface, clip, x,
                box.y + box.height - 1U - ring, shade);
        }
        for (y = box.y + ring; y < box.y + box.height - ring; ++y) {
            trait_surface_plot(surface, clip, box.x + ring, y, lit);
            trait_surface_plot(surface, clip,
                box.x + box.width - 1U - ring, y, shade);
        }
    }
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
 * THE BUTTONS, as fvwm puts them on a title bar: square, the full height
 * of the bar, flush against its end, and each one relieved like a key.
 *
 * They were eight-pixel marks floating in the bar with air around them,
 * which is Openbox's arrangement.  fvwm has no air: the bar ends in
 * buttons.
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
    uint32_t side = TRAIT_TITLE_HEIGHT;
    uint32_t right;

    if (window == NULL || out == NULL || title.width < side * 4U) {
        return false;
    }
    right = title.x + title.width;
    out->y = title.y;
    out->width = side;
    out->height = side;
    switch (which) {
    case TRAIT_WINDOW_CLOSE:
        out->x = right - side;
        return true;
    case TRAIT_WINDOW_MAXIMISE:
        out->x = right - side * 2U;
        return true;
    case TRAIT_WINDOW_MINIMISE:
        out->x = right - side * 3U;
        return true;
    default:
        return false;
    }
}

/* The mark inside a button, centred in it. */
static struct trait_rect mark_box(struct trait_rect button, uint32_t size)
{
    struct trait_rect box;

    box.x = button.x + (button.width - size) / 2U;
    box.y = button.y + (button.height - size) / 2U;
    box.width = size;
    box.height = size;
    return box;
}

static void outline(struct trait_surface *surface, struct trait_rect clip,
    struct trait_rect box, uint32_t ink)
{
    uint32_t at;

    for (at = 0U; at < box.width; ++at) {
        trait_surface_plot(surface, clip, box.x + at, box.y, ink);
        trait_surface_plot(surface, clip, box.x + at,
            box.y + box.height - 1U, ink);
    }
    for (at = 0U; at < box.height; ++at) {
        trait_surface_plot(surface, clip, box.x, box.y + at, ink);
        trait_surface_plot(surface, clip, box.x + box.width - 1U,
            box.y + at, ink);
    }
}

static void buttons(struct trait_surface *surface, struct trait_rect title,
    const struct trait_window *window, uint32_t ink,
    uint32_t lit, uint32_t shade)
{
    struct trait_rect box;
    struct trait_rect mark;
    uint32_t at;

    /*
     * MOTIF'S MARKS.  fvwm is running MWMDecor here - the sample file
     * says so on the Style "*" line - and Motif's two are a small square
     * for iconify and a large one for maximise, both outlines.  The
     * close cross is not Motif's, which puts closing on the window menu;
     * it is here because this shell has a close button and an unlabelled
     * one would be worse than a borrowed mark.
     */
    if (trait_window_button_bounds(window, TRAIT_WINDOW_MINIMISE, &box)) {
        relieve(surface, title, box, lit, shade, TRAIT_RELIEF_WIDTH);
        outline(surface, title, mark_box(box, 6U), ink);
    }
    if (trait_window_button_bounds(window, TRAIT_WINDOW_MAXIMISE, &box)) {
        relieve(surface, title, box, lit, shade, TRAIT_RELIEF_WIDTH);
        mark = mark_box(box, 12U);
        outline(surface, title, mark, ink);
        /* A MAXIMISED window's button shows the restore mark - a second
         * box inside the first - because a button that looks the same in
         * both states does not say which one you are in. */
        if (window->maximised) {
            outline(surface, title, mark_box(box, 6U), ink);
        }
    }
    if (trait_window_button_bounds(window, TRAIT_WINDOW_CLOSE, &box)) {
        relieve(surface, title, box, lit, shade, TRAIT_RELIEF_WIDTH);
        mark = mark_box(box, BUTTON_MARK);
        for (at = 0U; at < BUTTON_MARK; ++at) {
            trait_surface_plot(surface, title, mark.x + at, mark.y + at,
                               ink);
            trait_surface_plot(surface, title, mark.x + at,
                mark.y + BUTTON_MARK - 1U - at, ink);
        }
    }
}

/*
 * THE CORNER HANDLES.  fvwm cuts the border into four sides and four
 * corners and draws the cut, so you can see where a drag stops changing
 * one dimension and starts changing two.  The line is in the shadow
 * colour, which is how fvwm draws it: the pieces are all raised, so what
 * separates them is the dark side of a bevel with nothing on the other.
 */
static void handles(struct trait_surface *surface,
    struct trait_rect frame, uint32_t shade)
{
    uint32_t near_x = frame.x + TRAIT_CORNER;
    uint32_t far_x = frame.x + frame.width - 1U - TRAIT_CORNER;
    uint32_t near_y = frame.y + TRAIT_CORNER;
    uint32_t far_y = frame.y + frame.height - 1U - TRAIT_CORNER;
    uint32_t at;

    if (frame.width < TRAIT_CORNER * 3U || frame.height < TRAIT_CORNER * 3U) {
        return;
    }
    for (at = 0U; at < TRAIT_BORDER; ++at) {
        trait_surface_plot(surface, frame, near_x, frame.y + at, shade);
        trait_surface_plot(surface, frame, far_x, frame.y + at, shade);
        trait_surface_plot(surface, frame, near_x,
            frame.y + frame.height - 1U - at, shade);
        trait_surface_plot(surface, frame, far_x,
            frame.y + frame.height - 1U - at, shade);
        trait_surface_plot(surface, frame, frame.x + at, near_y, shade);
        trait_surface_plot(surface, frame, frame.x + at, far_y, shade);
        trait_surface_plot(surface, frame,
            frame.x + frame.width - 1U - at, near_y, shade);
        trait_surface_plot(surface, frame,
            frame.x + frame.width - 1U - at, far_y, shade);
    }
}

void trait_window_draw(struct trait_surface *surface,
    const struct trait_window *window)
{
    struct trait_rect title;
    struct trait_rect client;
    struct trait_rect edge;
    uint32_t ground;
    uint32_t lit;
    uint32_t shade;
    uint32_t ink;
    uint32_t width;

    if (window == NULL || !trait_surface_valid(surface)) {
        return;
    }
    title = trait_window_title(window);
    client = trait_window_client(window);
    ground = window->active ? TRAIT_FRAME_ACTIVE : TRAIT_FRAME_IDLE;
    lit = window->active ? TRAIT_FRAME_ACTIVE_HI : TRAIT_FRAME_IDLE_HI;
    shade = window->active ? TRAIT_FRAME_ACTIVE_LO : TRAIT_FRAME_IDLE_LO;
    ink = window->active ? TRAIT_FRAME_INK : TRAIT_FRAME_INK_DIM;

    /*
     * THE BORDER, AND ONLY THE BORDER.
     *
     * This used to be one fill of the whole frame with the client
     * painted over it afterwards, which is cheaper and was harmless for
     * as long as every application filled its client as its first act -
     * and every one of them does.  It stopped being harmless the moment
     * one of them wanted to be SEE-THROUGH.  Pseudo-transparency works
     * by reading the framebuffer back, and what the terminal read there
     * was this fill: its own frame, a flat colour, already covering the
     * desktop it was trying to show.  That is why a sheer terminal came
     * out one even tint wherever you put it, lighter than an opaque one
     * but no more transparent.
     *
     * So the frame draws four strips around the client and leaves what
     * is inside alone.  The window owns its border and its title bar;
     * what is under the client belongs to whatever is behind the
     * window, until the application covers it.
     */
    edge = window->frame;
    edge.height = client.y - window->frame.y;
    trait_surface_fill(surface, window->frame, edge, ground);

    edge = window->frame;
    edge.y = client.y + client.height;
    edge.height = window->frame.y + window->frame.height - edge.y;
    trait_surface_fill(surface, window->frame, edge, ground);

    edge = window->frame;
    edge.y = client.y;
    edge.height = client.height;
    edge.width = client.x - window->frame.x;
    trait_surface_fill(surface, window->frame, edge, ground);

    edge.x = client.x + client.width;
    edge.width = window->frame.x + window->frame.width - edge.x;
    trait_surface_fill(surface, window->frame, edge, ground);

    /*
     * The border is a RAISED bar: lit on the outside top and left, dark
     * on the outside bottom and right, and the reverse where it meets
     * what is inside it.  Everything else about an fvwm frame follows
     * from those two rectangles.
     */
    relieve(surface, window->frame, window->frame, lit, shade,
            TRAIT_RELIEF_WIDTH);
    edge.x = window->frame.x + TRAIT_BORDER - TRAIT_RELIEF_WIDTH;
    edge.y = window->frame.y + TRAIT_BORDER - TRAIT_RELIEF_WIDTH;
    width = TRAIT_BORDER * 2U - TRAIT_RELIEF_WIDTH * 2U;
    edge.width = window->frame.width > width ?
        window->frame.width - width : 0U;
    edge.height = window->frame.height > width ?
        window->frame.height - width : 0U;
    relieve(surface, window->frame, edge, shade, lit, TRAIT_RELIEF_WIDTH);
    handles(surface, window->frame, shade);

    /* The title bar is a raised bar of its own inside that. */
    relieve(surface, title, title, lit, shade, TRAIT_RELIEF_WIDTH);
    /*
     * CENTRED, which is fvwm's default and not this project's taste.  It
     * reads differently from a left-aligned title: the name of the
     * window is the label ON the bar rather than the first thing in a
     * row of things.
     */
    {
        uint32_t span = trait_font_width(window->title);
        uint32_t pen = title.width > span ?
            title.x + (title.width - span) / 2U : title.x + 2U;

        trait_font_draw(surface, title, pen,
            title.y + title.height - 5U, window->title, ink);
    }
    buttons(surface, title, window, ink, lit, shade);
}
