/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The Trait OS panel.  See trait/panel.h for what it is a copy of
 * and which pinned source each measurement came from; this file names the
 * file beside each value rather than restating the provenance.
 */
#include <trait/de/panel.h>

#include <trait/de/font.h>

#include "de_trait_panel_art.h"
#include "de_trait_mark.h"

/* ================================================================ METRICS
 *
 * The profile's own spacers, in the order it lists them.  `space N` is a
 * literal N pixels of nothing; lxpanel draws no separator for it.
 */
#define TRAIT_SPACE_LEAD 2U        /* space 2, before the menu */
#define TRAIT_SPACE_GAP 4U         /* space 4, three times over */

/* A launchbar button is the icon plus lxpanel's own padding either side. */
#define TRAIT_ICON 16U
#define TRAIT_BUTTON_PAD 3U
#define TRAIT_BUTTON (TRAIT_ICON + TRAIT_BUTTON_PAD * 2U)

/* pager: one cell per desktop, and the cell is the bar's height less the
 * border lxpanel draws round the group. */
#define TRAIT_PAGER_CELL 24U
#define TRAIT_PAGER_INSET 1U

/* dclock at %R is five characters; the plugin sizes itself to its text,
 * so this is a floor rather than the width. */
#define TRAIT_CLOCK_MIN 38U
#define TRAIT_CLOCK_PAD 6U

/* tray: lxpanel packs its icons at the bar's icon size with no padding
 * between them, which is what makes a tray read as a tray. */
#define TRAIT_TRAY_ICONS 1U

/* =============================================================== COLOURS
 *
 * background.png is 1x26 and lxpanel TILES it across the bar, so the bar
 * is exactly these twenty-six rows repeated.  They are the file's own
 * bytes, read out of lxpanel-data 0.11.1-2: the bright line at row 1, the
 * step down at row 12 and the lift at row 25 are what make the bar read
 * as lxpanel's rather than as a grey gradient, and a hand-written ramp is
 * a near-miss of something only twenty-six pixels tall.
 */
static const uint32_t TRAIT_PANEL_GROUND[TRAIT_PANEL_HEIGHT] = {
    0x000000U, 0xA3A3A3U, 0x6C6C6CU, 0x626262U, 0x595959U, 0x525252U,
    0x484848U, 0x3F3F3FU, 0x383838U, 0x323232U, 0x2C2C2CU, 0x262626U,
    0x040404U, 0x090909U, 0x0F0F0FU, 0x161616U, 0x1D1D1DU, 0x252525U,
    0x2C2C2CU, 0x353535U, 0x3E3E3EU, 0x474747U, 0x505050U, 0x5A5A5AU,
    0x636363U, 0x8B8B8BU
};

#define TRAIT_INK 0xFFFFFFU         /* Global { fontcolor=#ffffff } */
#define TRAIT_INK_DIM 0xB4B4B4U     /* a minimised task, half lit */

/* cpu.c: a black ground inside the border, and gdk_color_parse("green"),
 * which is X11 green and therefore #00FF00 exactly - not a chosen green. */
#define TRAIT_CPU_GROUND 0x000000U
#define TRAIT_CPU_INK 0x00FF00U
#define TRAIT_CPU_FRAME 0x555555U

/* taskbar { FlatButton=0 }, so a task is a raised button: lxpanel draws
 * it with the theme's light edge on top and its dark edge underneath. */
#define TRAIT_TASK_FACE 0x4C4C4CU
#define TRAIT_TASK_FACE_ACTIVE 0x6E6E6EU
#define TRAIT_TASK_LIGHT 0x7A7A7AU
#define TRAIT_TASK_DARK 0x1E1E1EU

#define TRAIT_PAGER_FACE 0x2A2A2AU
#define TRAIT_PAGER_CURRENT 0x5C7EA8U   /* the desk you are on */
#define TRAIT_PAGER_FRAME 0x6E6E6EU

/* ================================================================= STATE */

/*
 * The surface is HANDED IN rather than fetched, which is what every other
 * module here does: the panel draws where it is told to, so the same code
 * serves the framebuffer and the preview harness without either of them
 * being compiled into it.
 */
static struct trait_surface *canvas;

static bool panel_ready;
static struct trait_panel_task panel_tasks[TRAIT_PANEL_MAX_TASKS];
static bool panel_task_used[TRAIT_PANEL_MAX_TASKS];
static uint32_t panel_cpu[TRAIT_PANEL_CPU_COLUMNS];
static char panel_clock[16];
static uint32_t panel_volume = 65U;
static bool panel_muted;
static uint32_t panel_desktop;
static uint32_t panel_desktops = 2U;

/* ================================================================ HELPERS */

static uint32_t clamp_u32(uint32_t value, uint32_t high)
{
    return value > high ? high : value;
}

static size_t string_length(const char *text)
{
    size_t length = 0U;

    while (text != NULL && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void copy_label(char *out, const char *text, size_t capacity)
{
    size_t at = 0U;

    while (text != NULL && text[at] != '\0' && at + 1U < capacity) {
        out[at] = text[at];
        ++at;
    }
    out[at] = '\0';
}

/*
 * The art tables carry one plane per size and the sizes are listed in
 * trait_panel_art_size[].  A caller asks for the size it is drawing at,
 * and an exact match is the only match: scaling a sixteen-pixel icon to
 * seventeen is how an icon theme stops looking like itself.
 */
static uint32_t art_index_for(uint32_t size)
{
    uint32_t at;

    for (at = 0U; at < TRAIT_PANEL_ART_SIZES; ++at) {
        if (trait_panel_art_size[at] == size) {
            return at;
        }
    }
    return TRAIT_PANEL_ART_SIZES;
}

static const struct trait_panel_art_entry *art_named(const char *name)
{
    size_t at;
    size_t index;

    if (name == NULL) {
        return NULL;
    }
    for (index = 0U; index < TRAIT_PANEL_ART_COUNT; ++index) {
        const char *candidate = trait_panel_art[index].name;

        for (at = 0U; ; ++at) {
            if (candidate[at] != name[at]) {
                break;
            }
            if (candidate[at] == '\0') {
                return &trait_panel_art[index];
            }
        }
    }
    return NULL;
}

/* An icon, composited by its own alpha over whatever the bar put down. */
static void draw_icon(struct trait_rect clip,
    const char *name, uint32_t size, uint32_t left, uint32_t top)
{
    const struct trait_panel_art_entry *art = art_named(name);
    uint32_t plane = art_index_for(size);
    uint32_t x;
    uint32_t y;

    if (art == NULL || plane >= TRAIT_PANEL_ART_SIZES) {
        return;
    }
    for (y = 0U; y < size; ++y) {
        for (x = 0U; x < size; ++x) {
            uint32_t at = y * size + x;
            uint32_t alpha = art->alpha[plane][at];
            uint32_t under;

            if (alpha == 0U) {
                continue;
            }
            if (!trait_rect_contains(clip, left + x, top + y)) {
                continue;
            }
            under = trait_surface_read(canvas, left + x, top + y);
            trait_surface_plot(canvas, clip, left + x, top + y,
                 trait_blend(under, art->pixels[plane][at], alpha));
        }
    }
}

/*
 * The mark on the menu button.
 *
 * It is a PICTURE, not a silhouette: the onion carries its own colours,
 * so it is composited by its own alpha the way the launchbar's icons are.
 * An earlier mark here was a flat grey shape and this tinted it to the
 * bar's #ffffff; doing that to a picture turns it into a grey blob, which
 * is what happened the first time the onion went through this path.
 */
static void draw_mark(struct trait_rect clip, uint32_t size,
    uint32_t left, uint32_t top)
{
    const struct trait_mark_entry *mark = NULL;
    uint32_t plane = TRAIT_MARK_SIZES;
    uint32_t x;
    uint32_t y;
    uint32_t at;

    for (at = 0U; at < TRAIT_MARK_SIZES; ++at) {
        if (trait_mark_size[at] == size) {
            plane = at;
        }
    }
    /* BY NAME, not by position: the generator emits alphabetically, so an
     * icon added later can take any slot and a mark picked by index
     * silently becomes a different picture. */
    for (at = 0U; at < TRAIT_MARK_COUNT; ++at) {
        const char *name = trait_mark[at].name;

        if (name[0] == 'o' && name[1] == 'n' && name[2] == 'i' &&
                name[3] == 'o' && name[4] == 'n' && name[5] == '\0') {
            mark = &trait_mark[at];
        }
    }
    if (mark == NULL || plane >= TRAIT_MARK_SIZES) {
        return;
    }
    for (y = 0U; y < size; ++y) {
        for (x = 0U; x < size; ++x) {
            uint32_t index = y * size + x;
            uint32_t alpha = mark->alpha[plane][index];
            uint32_t under;

            if (alpha == 0U || !trait_rect_contains(clip, left + x, top + y)) {
                continue;
            }
            under = trait_surface_read(canvas, left + x, top + y);
            trait_surface_plot(canvas, clip, left + x, top + y,
                 trait_blend(under, mark->pixels[plane][index], alpha));
        }
    }
}

/* A raised button, the way a GTK2 theme draws one: light on top and left,
 * dark on bottom and right, the face between them. */
static void raised(struct trait_rect clip,
    struct trait_rect box, uint32_t face, uint32_t light, uint32_t dark)
{
    uint32_t at;

    trait_surface_fill(canvas, clip, box, face);
    for (at = 0U; at < box.width; ++at) {
        trait_surface_plot(canvas, clip, box.x + at, box.y, light);
        trait_surface_plot(canvas, clip, box.x + at, box.y + box.height - 1U, dark);
    }
    for (at = 0U; at < box.height; ++at) {
        trait_surface_plot(canvas, clip, box.x, box.y + at, light);
        trait_surface_plot(canvas, clip, box.x + box.width - 1U, box.y + at, dark);
    }
}

/* ================================================================ LAYOUT
 *
 * The bar is laid out from BOTH ends, because the profile is: everything
 * up to the taskbar is packed from the left, everything after it from the
 * right, and taskbar(expand=1) takes whatever is left between them.  That
 * is what expand=1 means, and it is why the clock does not move when a
 * window opens.
 */

static uint32_t launchbar_width(void)
{
    /* launchbar { pcmanfm, x-www-browser, terminal } - three buttons. */
    return TRAIT_BUTTON * 3U;
}

static uint32_t right_launchbar_width(void)
{
    /* Nought, and see trait_panel_draw(): the profile's screenlock and
     * logout icons are not in the vendored set at this size, and a
     * plugin with nothing to draw takes no room rather than reserving a
     * gap for a button that is not there. */
    return 0U;
}

static uint32_t pager_width(void)
{
    return panel_desktops * TRAIT_PAGER_CELL + TRAIT_PAGER_INSET * 2U;
}

static uint32_t clock_width(void)
{
    uint32_t text = trait_font_width(panel_clock);

    if (text == 0U) {
        text = TRAIT_CLOCK_MIN;
    }
    return text + TRAIT_CLOCK_PAD * 2U;
}

struct trait_rect trait_panel_bounds(struct trait_rect screen)
{
    struct trait_rect bar;

    bar.x = screen.x;
    bar.width = screen.width;
    bar.height = TRAIT_PANEL_HEIGHT;
    /* edge=bottom */
    bar.y = screen.y + screen.height - TRAIT_PANEL_HEIGHT;
    return bar;
}

enum trait_panel_status trait_panel_plugin_bounds(
    struct trait_rect screen, enum trait_panel_plugin which,
    struct trait_rect *out)
{
    struct trait_rect bar = trait_panel_bounds(screen);
    uint32_t left = bar.x + TRAIT_SPACE_LEAD;
    uint32_t right = bar.x + bar.width;
    uint32_t widths[TRAIT_PANEL_PLUGIN_COUNT];
    uint32_t at;

    if (out == NULL) {
        return TRAIT_PANEL_STATUS_NULL_ARGUMENT;
    }
    if (which >= TRAIT_PANEL_PLUGIN_COUNT) {
        return TRAIT_PANEL_STATUS_BAD_INDEX;
    }
    if (bar.width < 320U) {
        return TRAIT_PANEL_STATUS_UNSUPPORTED_GEOMETRY;
    }
    widths[TRAIT_PANEL_PLUGIN_MENU] = TRAIT_BUTTON;
    widths[TRAIT_PANEL_PLUGIN_LAUNCHBAR] = launchbar_width();
    widths[TRAIT_PANEL_PLUGIN_WINCMD] = TRAIT_BUTTON;
    widths[TRAIT_PANEL_PLUGIN_PAGER] = pager_width();
    widths[TRAIT_PANEL_PLUGIN_CPU] = TRAIT_PANEL_CPU_WIDTH;
    widths[TRAIT_PANEL_PLUGIN_VOLUME] = TRAIT_BUTTON;
    widths[TRAIT_PANEL_PLUGIN_TRAY] = TRAIT_BUTTON * TRAIT_TRAY_ICONS;
    widths[TRAIT_PANEL_PLUGIN_CLOCK] = clock_width();
    widths[TRAIT_PANEL_PLUGIN_LAUNCHBAR_RIGHT] = right_launchbar_width();

    out->y = bar.y;
    out->height = bar.height;

    /* Packed from the left: menu, launchbar, space, wincmd, space, pager,
     * space - and then the taskbar. */
    for (at = TRAIT_PANEL_PLUGIN_MENU; at < TRAIT_PANEL_PLUGIN_TASKBAR;
            ++at) {
        if ((enum trait_panel_plugin)at == which) {
            out->x = left;
            out->width = widths[at];
            return TRAIT_PANEL_STATUS_OK;
        }
        left += widths[at];
        if (at == TRAIT_PANEL_PLUGIN_LAUNCHBAR ||
                at == TRAIT_PANEL_PLUGIN_WINCMD ||
                at == TRAIT_PANEL_PLUGIN_PAGER) {
            left += TRAIT_SPACE_GAP;
        }
    }

    /* Packed from the right, backwards, so the ones nearest the edge are
     * placed first. */
    for (at = TRAIT_PANEL_PLUGIN_LAUNCHBAR_RIGHT;
            at > TRAIT_PANEL_PLUGIN_TASKBAR; --at) {
        right -= widths[at];
        if ((enum trait_panel_plugin)at == which) {
            out->x = right;
            out->width = widths[at];
            return TRAIT_PANEL_STATUS_OK;
        }
    }

    /* taskbar(expand=1) is the gap between the two. */
    out->x = left;
    out->width = right > left ? right - left : 0U;
    return TRAIT_PANEL_STATUS_OK;
}

/* ================================================================ DRAWING */

static void draw_ground(struct trait_rect bar)
{
    uint32_t x;
    uint32_t y;

    for (y = 0U; y < bar.height; ++y) {
        uint32_t colour = TRAIT_PANEL_GROUND[y % TRAIT_PANEL_HEIGHT];

        for (x = bar.x; x < bar.x + bar.width; ++x) {
            trait_surface_plot(canvas, bar, x, bar.y + y, colour);
        }
    }
}

static uint32_t middle(struct trait_rect box, uint32_t size)
{
    return box.y + (box.height - size) / 2U;
}

static void draw_launchbar(struct trait_rect box,
    const char *const *names, uint32_t count)
{
    uint32_t at;

    for (at = 0U; at < count; ++at) {
        draw_icon(box, names[at], TRAIT_ICON,
                  box.x + at * TRAIT_BUTTON + TRAIT_BUTTON_PAD,
                  middle(box, TRAIT_ICON));
    }
}

static void draw_pager(struct trait_rect box)
{
    uint32_t at;

    for (at = 0U; at < panel_desktops; ++at) {
        struct trait_rect cell;

        cell.x = box.x + TRAIT_PAGER_INSET + at * TRAIT_PAGER_CELL;
        cell.y = box.y + TRAIT_PAGER_INSET;
        cell.width = TRAIT_PAGER_CELL;
        cell.height = box.height - TRAIT_PAGER_INSET * 2U;
        trait_surface_fill(canvas, box, cell,
             at == panel_desktop ? TRAIT_PAGER_CURRENT : TRAIT_PAGER_FACE);
        {
            uint32_t edge;

            for (edge = 0U; edge < cell.width; ++edge) {
                trait_surface_plot(canvas, box, cell.x + edge, cell.y,
                     TRAIT_PAGER_FRAME);
                trait_surface_plot(canvas, box, cell.x + edge,
                     cell.y + cell.height - 1U, TRAIT_PAGER_FRAME);
            }
            for (edge = 0U; edge < cell.height; ++edge) {
                trait_surface_plot(canvas, box, cell.x, cell.y + edge, TRAIT_PAGER_FRAME);
                trait_surface_plot(canvas, box, cell.x + cell.width - 1U, cell.y + edge,
                     TRAIT_PAGER_FRAME);
            }
        }
    }
}

/*
 * Which task button is where.  draw_tasks() lays them out and this has to
 * agree with it exactly, so both go through here rather than each working
 * it out - a hit test that computes its own layout is a hit test that
 * drifts a pixel at a time until the wrong button answers.
 */
static bool task_button(struct trait_rect box, uint32_t drawn,
    uint32_t live, struct trait_rect *out)
{
    uint32_t width;

    if (live == 0U || box.width == 0U) {
        return false;
    }
    width = box.width / live;
    if (width > TRAIT_PANEL_MAX_TASK_WIDTH) {
        width = TRAIT_PANEL_MAX_TASK_WIDTH;
    }
    out->x = box.x + drawn * width;
    out->y = box.y + 2U;
    out->width = width > 2U ? width - 2U : width;
    out->height = box.height - 4U;
    return true;
}

static void draw_tasks(struct trait_rect box)
{
    uint32_t live = trait_panel_task_count();
    uint32_t drawn = 0U;
    uint32_t slot;

    if (live == 0U || box.width == 0U) {
        return;
    }
    for (slot = 0U; slot < TRAIT_PANEL_MAX_TASKS; ++slot) {
        struct trait_rect button;
        const struct trait_panel_task *task = &panel_tasks[slot];
        uint32_t text_left;

        if (!panel_task_used[slot] || task->desktop != panel_desktop) {
            continue;
        }
        if (!task_button(box, drawn, live, &button)) {
            continue;
        }
        raised(box, button,
               task->active ? TRAIT_TASK_FACE_ACTIVE : TRAIT_TASK_FACE,
               TRAIT_TASK_LIGHT, TRAIT_TASK_DARK);
        draw_icon(box, task->icon, TRAIT_ICON,
                  button.x + 3U, middle(button, TRAIT_ICON));
        text_left = button.x + 3U + TRAIT_ICON + 3U;
        {
            struct trait_rect text_clip;

            text_clip.x = text_left;
            text_clip.y = button.y;
            text_clip.width = button.x + button.width > text_left + 2U ?
                button.x + button.width - text_left - 2U : 0U;
            text_clip.height = button.height;
            trait_font_draw(canvas, text_clip, text_left,
                button.y + button.height - 6U, task->label,
                task->minimised ? TRAIT_INK_DIM : TRAIT_INK);
        }
        ++drawn;
    }
}

/*
 * cpu.c draws a black box with a border and then, for each column, a bar
 * rising from the bottom in proportion to that sample.  The oldest sample
 * is at the left, so the graph reads left to right like everything else.
 */
static void draw_cpu(struct trait_rect box)
{
    struct trait_rect inner;
    uint32_t at;

    inner.x = box.x + TRAIT_PANEL_CPU_BORDER;
    inner.y = box.y + TRAIT_PANEL_CPU_BORDER;
    inner.width = box.width - TRAIT_PANEL_CPU_BORDER * 2U;
    inner.height = box.height - TRAIT_PANEL_CPU_BORDER * 2U;
    trait_surface_fill(canvas, box, box, TRAIT_CPU_FRAME);
    trait_surface_fill(canvas, box, inner, TRAIT_CPU_GROUND);
    for (at = 0U; at < TRAIT_PANEL_CPU_COLUMNS &&
            at < inner.width; ++at) {
        uint32_t lit = panel_cpu[at] * inner.height / 100U;
        uint32_t row;

        for (row = 0U; row < lit; ++row) {
            trait_surface_plot(canvas, box, inner.x + at,
                 inner.y + inner.height - 1U - row, TRAIT_CPU_INK);
        }
    }
}

static void draw_clock(struct trait_rect box)
{
    uint32_t text = trait_font_width(panel_clock);

    trait_font_draw(canvas, box,
        box.x + (box.width > text ? (box.width - text) / 2U : 0U),
        box.y + box.height - 8U, panel_clock, TRAIT_INK);
}

struct trait_panel_hit trait_panel_hit(struct trait_rect screen,
    uint32_t x, uint32_t y)
{
    struct trait_panel_hit hit = { TRAIT_PANEL_HIT_NONE, 0U };
    struct trait_rect box;
    uint32_t at;

    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_MENU, &box) ==
            TRAIT_PANEL_STATUS_OK &&
            trait_rect_contains(box, x, y)) {
        hit.kind = TRAIT_PANEL_HIT_MENU;
        return hit;
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_LAUNCHBAR,
            &box) == TRAIT_PANEL_STATUS_OK &&
            trait_rect_contains(box, x, y)) {
        hit.kind = TRAIT_PANEL_HIT_LAUNCHER;
        hit.index = (x - box.x) / TRAIT_BUTTON;
        return hit;
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_WINCMD,
            &box) == TRAIT_PANEL_STATUS_OK &&
            trait_rect_contains(box, x, y)) {
        hit.kind = TRAIT_PANEL_HIT_WINCMD;
        return hit;
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_PAGER,
            &box) == TRAIT_PANEL_STATUS_OK &&
            trait_rect_contains(box, x, y)) {
        uint32_t cell = (x - box.x - TRAIT_PAGER_INSET) / TRAIT_PAGER_CELL;

        hit.kind = TRAIT_PANEL_HIT_PAGER;
        hit.index = cell < panel_desktops ? cell : panel_desktops - 1U;
        return hit;
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_TASKBAR,
            &box) == TRAIT_PANEL_STATUS_OK &&
            trait_rect_contains(box, x, y)) {
        uint32_t live = trait_panel_task_count();
        uint32_t drawn = 0U;

        for (at = 0U; at < TRAIT_PANEL_MAX_TASKS; ++at) {
            struct trait_rect button;

            if (!panel_task_used[at] ||
                    panel_tasks[at].desktop != panel_desktop) {
                continue;
            }
            if (task_button(box, drawn, live, &button) &&
                    trait_rect_contains(button, x, y)) {
                hit.kind = TRAIT_PANEL_HIT_TASK;
                hit.index = at;
                return hit;
            }
            ++drawn;
        }
        return hit;
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_VOLUME,
            &box) == TRAIT_PANEL_STATUS_OK &&
            trait_rect_contains(box, x, y)) {
        hit.kind = TRAIT_PANEL_HIT_VOLUME;
        return hit;
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_CLOCK,
            &box) == TRAIT_PANEL_STATUS_OK &&
            trait_rect_contains(box, x, y)) {
        hit.kind = TRAIT_PANEL_HIT_CLOCK;
        return hit;
    }
    return hit;
}

enum trait_panel_status trait_panel_draw(struct trait_rect screen)
{
    static const char *const LEFT_LAUNCHERS[3] = {
        "file-manager", "browser", "terminal"
    };
    /*
     * The profile's right-hand launchbar is { screenlock, logout }.
     * Neither icon is in the vendored set at 16 pixels, and drawing the
     * preferences cog in their place - which the first cut of this did -
     * is a control drawn as something it is not, which is the one thing
     * this shell does not do.  So the launchbar is EMPTY until the real
     * icon is here, and an empty plugin takes no width: lxpanel does not
     * draw a button it has no icon for either.
     */
    static const char *const *const RIGHT_LAUNCHERS = NULL;
    struct trait_rect bar = trait_panel_bounds(screen);
    struct trait_rect box;

    if (!panel_ready) {
        return TRAIT_PANEL_STATUS_NOT_INITIALIZED;
    }
    if (canvas == NULL) {
        return TRAIT_PANEL_STATUS_SURFACE_FAILURE;
    }
    if (bar.width < 320U) {
        return TRAIT_PANEL_STATUS_UNSUPPORTED_GEOMETRY;
    }
    draw_ground(bar);

    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_MENU, &box) ==
            TRAIT_PANEL_STATUS_OK) {
        draw_mark(box, TRAIT_ICON,
                  box.x + TRAIT_BUTTON_PAD, middle(box, TRAIT_ICON));
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_LAUNCHBAR,
            &box) == TRAIT_PANEL_STATUS_OK) {
        draw_launchbar(box, LEFT_LAUNCHERS, 3U);
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_WINCMD,
            &box) == TRAIT_PANEL_STATUS_OK) {
        draw_icon(box, "wincmd", TRAIT_ICON,
                  box.x + TRAIT_BUTTON_PAD, middle(box, TRAIT_ICON));
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_PAGER,
            &box) == TRAIT_PANEL_STATUS_OK) {
        draw_pager(box);
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_TASKBAR,
            &box) == TRAIT_PANEL_STATUS_OK) {
        draw_tasks(box);
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_CPU,
            &box) == TRAIT_PANEL_STATUS_OK) {
        draw_cpu(box);
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_VOLUME,
            &box) == TRAIT_PANEL_STATUS_OK) {
        draw_icon(box,
                  (panel_muted || panel_volume == 0U) ?
                      "volume-muted" : "volume", TRAIT_ICON,
                  box.x + TRAIT_BUTTON_PAD, middle(box, TRAIT_ICON));
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_TRAY,
            &box) == TRAIT_PANEL_STATUS_OK) {
        draw_icon(box, "network", TRAIT_ICON,
                  box.x + TRAIT_BUTTON_PAD, middle(box, TRAIT_ICON));
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_CLOCK,
            &box) == TRAIT_PANEL_STATUS_OK) {
        draw_clock(box);
    }
    if (trait_panel_plugin_bounds(screen,
            TRAIT_PANEL_PLUGIN_LAUNCHBAR_RIGHT, &box) ==
            TRAIT_PANEL_STATUS_OK) {
        draw_launchbar(box, RIGHT_LAUNCHERS, 0U);
    }
    return TRAIT_PANEL_STATUS_OK;
}

/* =================================================================== API */

enum trait_panel_status trait_panel_attach(struct trait_surface *surface)
{
    if (surface == NULL) {
        return TRAIT_PANEL_STATUS_NULL_ARGUMENT;
    }
    canvas = surface;
    return TRAIT_PANEL_STATUS_OK;
}

enum trait_panel_status trait_panel_initialize(void)
{
    uint32_t at;

    for (at = 0U; at < TRAIT_PANEL_MAX_TASKS; ++at) {
        panel_task_used[at] = false;
    }
    for (at = 0U; at < TRAIT_PANEL_CPU_COLUMNS; ++at) {
        panel_cpu[at] = 0U;
    }
    copy_label(panel_clock, "00:00", sizeof(panel_clock));
    panel_volume = 65U;
    panel_muted = false;
    panel_desktop = 0U;
    panel_desktops = 2U;
    panel_ready = true;
    return TRAIT_PANEL_STATUS_OK;
}

bool trait_panel_is_initialized(void)
{
    return panel_ready;
}

enum trait_panel_status trait_panel_set_task(
    uint32_t slot, const struct trait_panel_task *task)
{
    if (task == NULL) {
        return TRAIT_PANEL_STATUS_NULL_ARGUMENT;
    }
    if (!panel_ready) {
        return TRAIT_PANEL_STATUS_NOT_INITIALIZED;
    }
    if (slot >= TRAIT_PANEL_MAX_TASKS) {
        return TRAIT_PANEL_STATUS_BAD_INDEX;
    }
    panel_tasks[slot] = *task;
    copy_label(panel_tasks[slot].label, task->label,
               TRAIT_PANEL_LABEL_BYTES);
    panel_task_used[slot] = true;
    return TRAIT_PANEL_STATUS_OK;
}

enum trait_panel_status trait_panel_clear_task(uint32_t slot)
{
    if (!panel_ready) {
        return TRAIT_PANEL_STATUS_NOT_INITIALIZED;
    }
    if (slot >= TRAIT_PANEL_MAX_TASKS) {
        return TRAIT_PANEL_STATUS_BAD_INDEX;
    }
    panel_task_used[slot] = false;
    return TRAIT_PANEL_STATUS_OK;
}

uint32_t trait_panel_task_count(void)
{
    uint32_t count = 0U;
    uint32_t at;

    for (at = 0U; at < TRAIT_PANEL_MAX_TASKS; ++at) {
        if (panel_task_used[at] &&
                panel_tasks[at].desktop == panel_desktop) {
            ++count;
        }
    }
    return count;
}

enum trait_panel_status trait_panel_push_cpu(uint32_t percent)
{
    uint32_t at;

    if (!panel_ready) {
        return TRAIT_PANEL_STATUS_NOT_INITIALIZED;
    }
    for (at = 1U; at < TRAIT_PANEL_CPU_COLUMNS; ++at) {
        panel_cpu[at - 1U] = panel_cpu[at];
    }
    panel_cpu[TRAIT_PANEL_CPU_COLUMNS - 1U] = clamp_u32(percent, 100U);
    return TRAIT_PANEL_STATUS_OK;
}

enum trait_panel_status trait_panel_set_clock(const char *text)
{
    if (text == NULL) {
        return TRAIT_PANEL_STATUS_NULL_ARGUMENT;
    }
    if (!panel_ready) {
        return TRAIT_PANEL_STATUS_NOT_INITIALIZED;
    }
    copy_label(panel_clock, text, sizeof(panel_clock));
    return TRAIT_PANEL_STATUS_OK;
}

enum trait_panel_status trait_panel_set_volume(uint32_t level, bool muted)
{
    if (!panel_ready) {
        return TRAIT_PANEL_STATUS_NOT_INITIALIZED;
    }
    panel_volume = clamp_u32(level, 100U);
    panel_muted = muted;
    return TRAIT_PANEL_STATUS_OK;
}

enum trait_panel_status trait_panel_set_desktop(uint32_t current,
    uint32_t count)
{
    if (!panel_ready) {
        return TRAIT_PANEL_STATUS_NOT_INITIALIZED;
    }
    if (count == 0U || count > TRAIT_PANEL_MAX_DESKTOPS ||
            current >= count) {
        return TRAIT_PANEL_STATUS_BAD_INDEX;
    }
    panel_desktop = current;
    panel_desktops = count;
    return TRAIT_PANEL_STATUS_OK;
}

const char *trait_panel_status_string(enum trait_panel_status status)
{
    switch (status) {
    case TRAIT_PANEL_STATUS_OK:
        return "ok";
    case TRAIT_PANEL_STATUS_NULL_ARGUMENT:
        return "null argument";
    case TRAIT_PANEL_STATUS_NOT_INITIALIZED:
        return "not initialized";
    case TRAIT_PANEL_STATUS_BAD_INDEX:
        return "bad index";
    case TRAIT_PANEL_STATUS_UNSUPPORTED_GEOMETRY:
        return "unsupported geometry";
    case TRAIT_PANEL_STATUS_SURFACE_FAILURE:
        return "surface failure";
    case TRAIT_PANEL_STATUS_FONT_FAILURE:
        return "font failure";
    default:
        return "unknown";
    }
}

/*
 * The self test asks the layout the one question the profile answers:
 * does the taskbar really take the gap, so that the clock does not move
 * when a window opens?  A panel whose clock drifts is not this panel.
 */
bool trait_panel_self_test(void)
{
    struct trait_rect screen = { 0U, 0U, 1280U, 800U };
    struct trait_rect clock_empty;
    struct trait_rect clock_busy;
    struct trait_panel_task task;
    uint32_t at;

    if (trait_panel_initialize() != TRAIT_PANEL_STATUS_OK) {
        return false;
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_CLOCK,
            &clock_empty) != TRAIT_PANEL_STATUS_OK) {
        return false;
    }
    for (at = 0U; at < 4U; ++at) {
        task.icon = "terminal";
        task.active = at == 0U;
        task.minimised = false;
        task.desktop = 0U;
        copy_label(task.label, "a window", TRAIT_PANEL_LABEL_BYTES);
        if (trait_panel_set_task(at, &task) != TRAIT_PANEL_STATUS_OK) {
            return false;
        }
    }
    if (trait_panel_plugin_bounds(screen, TRAIT_PANEL_PLUGIN_CLOCK,
            &clock_busy) != TRAIT_PANEL_STATUS_OK) {
        return false;
    }
    if (clock_empty.x != clock_busy.x) {
        return false;
    }
    if (string_length(panel_clock) == 0U) {
        return false;
    }
    return true;
}
