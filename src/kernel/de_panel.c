/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The OpenGAT panel.  See opengat/panel.h for what it is a copy of
 * and which pinned source each measurement came from; this file names the
 * file beside each value rather than restating the provenance.
 */
#include <opengat/de/panel.h>

#include <opengat/de/font.h>

#include "de_opengat_panel_art.h"
#include "de_opengat_mark.h"

/* ================================================================ METRICS
 *
 * The profile's own spacers, in the order it lists them.  `space N` is a
 * literal N pixels of nothing; lxpanel draws no separator for it.
 */
#define OPENGAT_SPACE_LEAD 2U        /* space 2, before the menu */
#define OPENGAT_SPACE_GAP 4U         /* space 4, three times over */

/* A launchbar button is the icon plus lxpanel's own padding either side. */
#define OPENGAT_ICON 16U
#define OPENGAT_BUTTON_PAD 3U
#define OPENGAT_BUTTON (OPENGAT_ICON + OPENGAT_BUTTON_PAD * 2U)

/* pager: one cell per desktop, and the cell is the bar's height less the
 * border lxpanel draws round the group. */
#define OPENGAT_PAGER_CELL 24U
#define OPENGAT_PAGER_INSET 1U

/* dclock at %R is five characters; the plugin sizes itself to its text,
 * so this is a floor rather than the width. */
#define OPENGAT_CLOCK_MIN 38U
#define OPENGAT_CLOCK_PAD 6U

/* tray: lxpanel packs its icons at the bar's icon size with no padding
 * between them, which is what makes a tray read as a tray. */
#define OPENGAT_TRAY_ICONS 1U

/* =============================================================== COLOURS
 *
 * background.png is 1x26 and lxpanel TILES it across the bar, so the bar
 * is exactly these twenty-six rows repeated.  They are the file's own
 * bytes, read out of lxpanel-data 0.11.1-2: the bright line at row 1, the
 * step down at row 12 and the lift at row 25 are what make the bar read
 * as lxpanel's rather than as a grey gradient, and a hand-written ramp is
 * a near-miss of something only twenty-six pixels tall.
 */
static const uint32_t OPENGAT_PANEL_GROUND[OPENGAT_PANEL_HEIGHT] = {
    0x000000U, 0xA3A3A3U, 0x6C6C6CU, 0x626262U, 0x595959U, 0x525252U,
    0x484848U, 0x3F3F3FU, 0x383838U, 0x323232U, 0x2C2C2CU, 0x262626U,
    0x040404U, 0x090909U, 0x0F0F0FU, 0x161616U, 0x1D1D1DU, 0x252525U,
    0x2C2C2CU, 0x353535U, 0x3E3E3EU, 0x474747U, 0x505050U, 0x5A5A5AU,
    0x636363U, 0x8B8B8BU
};

#define OPENGAT_INK 0xFFFFFFU         /* Global { fontcolor=#ffffff } */
#define OPENGAT_INK_DIM 0xB4B4B4U     /* a minimised task, half lit */

/* cpu.c: a black ground inside the border, and gdk_color_parse("green"),
 * which is X11 green and therefore #00FF00 exactly - not a chosen green. */
#define OPENGAT_CPU_GROUND 0x000000U
#define OPENGAT_CPU_INK 0x00FF00U
#define OPENGAT_CPU_FRAME 0x555555U

/* taskbar { FlatButton=0 }, so a task is a raised button: lxpanel draws
 * it with the theme's light edge on top and its dark edge underneath. */
#define OPENGAT_TASK_FACE 0x4C4C4CU
#define OPENGAT_TASK_FACE_ACTIVE 0x6E6E6EU
#define OPENGAT_TASK_LIGHT 0x7A7A7AU
#define OPENGAT_TASK_DARK 0x1E1E1EU

#define OPENGAT_PAGER_FACE 0x2A2A2AU
#define OPENGAT_PAGER_CURRENT 0x5C7EA8U   /* the desk you are on */
#define OPENGAT_PAGER_FRAME 0x6E6E6EU

/* ================================================================= STATE */

/*
 * The surface is HANDED IN rather than fetched, which is what every other
 * module here does: the panel draws where it is told to, so the same code
 * serves the framebuffer and the preview harness without either of them
 * being compiled into it.
 */
static struct opengat_surface *canvas;

static bool panel_ready;
static struct opengat_panel_task panel_tasks[OPENGAT_PANEL_MAX_TASKS];
static bool panel_task_used[OPENGAT_PANEL_MAX_TASKS];
static uint32_t panel_cpu[OPENGAT_PANEL_CPU_COLUMNS];
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
 * opengat_panel_art_size[].  A caller asks for the size it is drawing at,
 * and an exact match is the only match: scaling a sixteen-pixel icon to
 * seventeen is how an icon theme stops looking like itself.
 */
static uint32_t art_index_for(uint32_t size)
{
    uint32_t at;

    for (at = 0U; at < OPENGAT_PANEL_ART_SIZES; ++at) {
        if (opengat_panel_art_size[at] == size) {
            return at;
        }
    }
    return OPENGAT_PANEL_ART_SIZES;
}

static const struct opengat_panel_art_entry *art_named(const char *name)
{
    size_t at;
    size_t index;

    if (name == NULL) {
        return NULL;
    }
    for (index = 0U; index < OPENGAT_PANEL_ART_COUNT; ++index) {
        const char *candidate = opengat_panel_art[index].name;

        for (at = 0U; ; ++at) {
            if (candidate[at] != name[at]) {
                break;
            }
            if (candidate[at] == '\0') {
                return &opengat_panel_art[index];
            }
        }
    }
    return NULL;
}

/* An icon, composited by its own alpha over whatever the bar put down. */
static void draw_icon(struct opengat_rect clip,
    const char *name, uint32_t size, uint32_t left, uint32_t top)
{
    const struct opengat_panel_art_entry *art = art_named(name);
    uint32_t plane = art_index_for(size);
    uint32_t x;
    uint32_t y;

    if (art == NULL || plane >= OPENGAT_PANEL_ART_SIZES) {
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
            if (!opengat_rect_contains(clip, left + x, top + y)) {
                continue;
            }
            under = opengat_surface_read(canvas, left + x, top + y);
            opengat_surface_plot(canvas, clip, left + x, top + y,
                 opengat_blend(under, art->pixels[plane][at], alpha));
        }
    }
}

/*
 * The mark on the menu button.
 *
 * The OpenGAT G keeps its own navy and blue-grey planes, so the panel
 * composites the source colours through the generated alpha mask.
 */
static void draw_mark(struct opengat_rect clip, uint32_t size,
    uint32_t left, uint32_t top)
{
    const struct opengat_mark_entry *mark = NULL;
    uint32_t plane = OPENGAT_MARK_SIZES;
    uint32_t x;
    uint32_t y;
    uint32_t at;

    for (at = 0U; at < OPENGAT_MARK_SIZES; ++at) {
        if (opengat_mark_size[at] == size) {
            plane = at;
        }
    }
    /* BY NAME, not by position: the generator emits alphabetically, so an
     * icon added later can take any slot and a mark picked by index
     * silently becomes a different picture. */
    for (at = 0U; at < OPENGAT_MARK_COUNT; ++at) {
        const char *name = opengat_mark[at].name;

        if (name[0] == 'o' && name[1] == 'p' && name[2] == 'e' &&
                name[3] == 'n' && name[4] == 'g' && name[5] == 'a' &&
                name[6] == 't' && name[7] == '\0') {
            mark = &opengat_mark[at];
        }
    }
    if (mark == NULL || plane >= OPENGAT_MARK_SIZES) {
        return;
    }
    for (y = 0U; y < size; ++y) {
        for (x = 0U; x < size; ++x) {
            uint32_t index = y * size + x;
            uint32_t alpha = mark->alpha[plane][index];
            uint32_t under;

            if (alpha == 0U || !opengat_rect_contains(clip, left + x, top + y)) {
                continue;
            }
            under = opengat_surface_read(canvas, left + x, top + y);
            opengat_surface_plot(canvas, clip, left + x, top + y,
                 opengat_blend(under, mark->pixels[plane][index], alpha));
        }
    }
}

/* A raised button, the way a GTK2 theme draws one: light on top and left,
 * dark on bottom and right, the face between them. */
static void raised(struct opengat_rect clip,
    struct opengat_rect box, uint32_t face, uint32_t light, uint32_t dark)
{
    uint32_t at;

    opengat_surface_fill(canvas, clip, box, face);
    for (at = 0U; at < box.width; ++at) {
        opengat_surface_plot(canvas, clip, box.x + at, box.y, light);
        opengat_surface_plot(canvas, clip, box.x + at, box.y + box.height - 1U, dark);
    }
    for (at = 0U; at < box.height; ++at) {
        opengat_surface_plot(canvas, clip, box.x, box.y + at, light);
        opengat_surface_plot(canvas, clip, box.x + box.width - 1U, box.y + at, dark);
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
    return OPENGAT_BUTTON * 3U;
}

static uint32_t right_launchbar_width(void)
{
    /* Nought, and see opengat_panel_draw(): the profile's screenlock and
     * logout icons are not in the vendored set at this size, and a
     * plugin with nothing to draw takes no room rather than reserving a
     * gap for a button that is not there. */
    return 0U;
}

static uint32_t pager_width(void)
{
    return panel_desktops * OPENGAT_PAGER_CELL + OPENGAT_PAGER_INSET * 2U;
}

static uint32_t clock_width(void)
{
    uint32_t text = opengat_font_width(panel_clock);

    if (text == 0U) {
        text = OPENGAT_CLOCK_MIN;
    }
    return text + OPENGAT_CLOCK_PAD * 2U;
}

struct opengat_rect opengat_panel_bounds(struct opengat_rect screen)
{
    struct opengat_rect bar;

    bar.x = screen.x;
    bar.width = screen.width;
    bar.height = OPENGAT_PANEL_HEIGHT;
    /* edge=bottom */
    bar.y = screen.y + screen.height - OPENGAT_PANEL_HEIGHT;
    return bar;
}

enum opengat_panel_status opengat_panel_plugin_bounds(
    struct opengat_rect screen, enum opengat_panel_plugin which,
    struct opengat_rect *out)
{
    struct opengat_rect bar = opengat_panel_bounds(screen);
    uint32_t left = bar.x + OPENGAT_SPACE_LEAD;
    uint32_t right = bar.x + bar.width;
    uint32_t widths[OPENGAT_PANEL_PLUGIN_COUNT];
    uint32_t at;

    if (out == NULL) {
        return OPENGAT_PANEL_STATUS_NULL_ARGUMENT;
    }
    if (which >= OPENGAT_PANEL_PLUGIN_COUNT) {
        return OPENGAT_PANEL_STATUS_BAD_INDEX;
    }
    if (bar.width < 320U) {
        return OPENGAT_PANEL_STATUS_UNSUPPORTED_GEOMETRY;
    }
    widths[OPENGAT_PANEL_PLUGIN_MENU] = OPENGAT_BUTTON;
    widths[OPENGAT_PANEL_PLUGIN_LAUNCHBAR] = launchbar_width();
    widths[OPENGAT_PANEL_PLUGIN_WINCMD] = OPENGAT_BUTTON;
    widths[OPENGAT_PANEL_PLUGIN_PAGER] = pager_width();
    widths[OPENGAT_PANEL_PLUGIN_CPU] = OPENGAT_PANEL_CPU_WIDTH;
    widths[OPENGAT_PANEL_PLUGIN_VOLUME] = OPENGAT_BUTTON;
    widths[OPENGAT_PANEL_PLUGIN_TRAY] = OPENGAT_BUTTON * OPENGAT_TRAY_ICONS;
    widths[OPENGAT_PANEL_PLUGIN_CLOCK] = clock_width();
    widths[OPENGAT_PANEL_PLUGIN_LAUNCHBAR_RIGHT] = right_launchbar_width();

    out->y = bar.y;
    out->height = bar.height;

    /* Packed from the left: menu, launchbar, space, wincmd, space, pager,
     * space - and then the taskbar. */
    for (at = OPENGAT_PANEL_PLUGIN_MENU; at < OPENGAT_PANEL_PLUGIN_TASKBAR;
            ++at) {
        if ((enum opengat_panel_plugin)at == which) {
            out->x = left;
            out->width = widths[at];
            return OPENGAT_PANEL_STATUS_OK;
        }
        left += widths[at];
        if (at == OPENGAT_PANEL_PLUGIN_LAUNCHBAR ||
                at == OPENGAT_PANEL_PLUGIN_WINCMD ||
                at == OPENGAT_PANEL_PLUGIN_PAGER) {
            left += OPENGAT_SPACE_GAP;
        }
    }

    /* Packed from the right, backwards, so the ones nearest the edge are
     * placed first. */
    for (at = OPENGAT_PANEL_PLUGIN_LAUNCHBAR_RIGHT;
            at > OPENGAT_PANEL_PLUGIN_TASKBAR; --at) {
        right -= widths[at];
        if ((enum opengat_panel_plugin)at == which) {
            out->x = right;
            out->width = widths[at];
            return OPENGAT_PANEL_STATUS_OK;
        }
    }

    /* taskbar(expand=1) is the gap between the two. */
    out->x = left;
    out->width = right > left ? right - left : 0U;
    return OPENGAT_PANEL_STATUS_OK;
}

/* ================================================================ DRAWING */

static void draw_ground(struct opengat_rect bar)
{
    uint32_t x;
    uint32_t y;

    for (y = 0U; y < bar.height; ++y) {
        uint32_t colour = OPENGAT_PANEL_GROUND[y % OPENGAT_PANEL_HEIGHT];

        for (x = bar.x; x < bar.x + bar.width; ++x) {
            opengat_surface_plot(canvas, bar, x, bar.y + y, colour);
        }
    }
}

static uint32_t middle(struct opengat_rect box, uint32_t size)
{
    return box.y + (box.height - size) / 2U;
}

static void draw_launchbar(struct opengat_rect box,
    const char *const *names, uint32_t count)
{
    uint32_t at;

    for (at = 0U; at < count; ++at) {
        draw_icon(box, names[at], OPENGAT_ICON,
                  box.x + at * OPENGAT_BUTTON + OPENGAT_BUTTON_PAD,
                  middle(box, OPENGAT_ICON));
    }
}

static void draw_pager(struct opengat_rect box)
{
    uint32_t at;

    for (at = 0U; at < panel_desktops; ++at) {
        struct opengat_rect cell;

        cell.x = box.x + OPENGAT_PAGER_INSET + at * OPENGAT_PAGER_CELL;
        cell.y = box.y + OPENGAT_PAGER_INSET;
        cell.width = OPENGAT_PAGER_CELL;
        cell.height = box.height - OPENGAT_PAGER_INSET * 2U;
        opengat_surface_fill(canvas, box, cell,
             at == panel_desktop ? OPENGAT_PAGER_CURRENT : OPENGAT_PAGER_FACE);
        {
            uint32_t edge;

            for (edge = 0U; edge < cell.width; ++edge) {
                opengat_surface_plot(canvas, box, cell.x + edge, cell.y,
                     OPENGAT_PAGER_FRAME);
                opengat_surface_plot(canvas, box, cell.x + edge,
                     cell.y + cell.height - 1U, OPENGAT_PAGER_FRAME);
            }
            for (edge = 0U; edge < cell.height; ++edge) {
                opengat_surface_plot(canvas, box, cell.x, cell.y + edge, OPENGAT_PAGER_FRAME);
                opengat_surface_plot(canvas, box, cell.x + cell.width - 1U, cell.y + edge,
                     OPENGAT_PAGER_FRAME);
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
static bool task_button(struct opengat_rect box, uint32_t drawn,
    uint32_t live, struct opengat_rect *out)
{
    uint32_t width;

    if (live == 0U || box.width == 0U) {
        return false;
    }
    width = box.width / live;
    if (width > OPENGAT_PANEL_MAX_TASK_WIDTH) {
        width = OPENGAT_PANEL_MAX_TASK_WIDTH;
    }
    out->x = box.x + drawn * width;
    out->y = box.y + 2U;
    out->width = width > 2U ? width - 2U : width;
    out->height = box.height - 4U;
    return true;
}

static void draw_tasks(struct opengat_rect box)
{
    uint32_t live = opengat_panel_task_count();
    uint32_t drawn = 0U;
    uint32_t slot;

    if (live == 0U || box.width == 0U) {
        return;
    }
    for (slot = 0U; slot < OPENGAT_PANEL_MAX_TASKS; ++slot) {
        struct opengat_rect button;
        const struct opengat_panel_task *task = &panel_tasks[slot];
        uint32_t text_left;

        if (!panel_task_used[slot] || task->desktop != panel_desktop) {
            continue;
        }
        if (!task_button(box, drawn, live, &button)) {
            continue;
        }
        raised(box, button,
               task->active ? OPENGAT_TASK_FACE_ACTIVE : OPENGAT_TASK_FACE,
               OPENGAT_TASK_LIGHT, OPENGAT_TASK_DARK);
        draw_icon(box, task->icon, OPENGAT_ICON,
                  button.x + 3U, middle(button, OPENGAT_ICON));
        text_left = button.x + 3U + OPENGAT_ICON + 3U;
        {
            struct opengat_rect text_clip;

            text_clip.x = text_left;
            text_clip.y = button.y;
            text_clip.width = button.x + button.width > text_left + 2U ?
                button.x + button.width - text_left - 2U : 0U;
            text_clip.height = button.height;
            opengat_font_draw(canvas, text_clip, text_left,
                button.y + button.height - 6U, task->label,
                task->minimised ? OPENGAT_INK_DIM : OPENGAT_INK);
        }
        ++drawn;
    }
}

/*
 * cpu.c draws a black box with a border and then, for each column, a bar
 * rising from the bottom in proportion to that sample.  The oldest sample
 * is at the left, so the graph reads left to right like everything else.
 */
static void draw_cpu(struct opengat_rect box)
{
    struct opengat_rect inner;
    uint32_t at;

    inner.x = box.x + OPENGAT_PANEL_CPU_BORDER;
    inner.y = box.y + OPENGAT_PANEL_CPU_BORDER;
    inner.width = box.width - OPENGAT_PANEL_CPU_BORDER * 2U;
    inner.height = box.height - OPENGAT_PANEL_CPU_BORDER * 2U;
    opengat_surface_fill(canvas, box, box, OPENGAT_CPU_FRAME);
    opengat_surface_fill(canvas, box, inner, OPENGAT_CPU_GROUND);
    for (at = 0U; at < OPENGAT_PANEL_CPU_COLUMNS &&
            at < inner.width; ++at) {
        uint32_t lit = panel_cpu[at] * inner.height / 100U;
        uint32_t row;

        for (row = 0U; row < lit; ++row) {
            opengat_surface_plot(canvas, box, inner.x + at,
                 inner.y + inner.height - 1U - row, OPENGAT_CPU_INK);
        }
    }
}

static void draw_clock(struct opengat_rect box)
{
    uint32_t text = opengat_font_width(panel_clock);

    opengat_font_draw(canvas, box,
        box.x + (box.width > text ? (box.width - text) / 2U : 0U),
        box.y + box.height - 8U, panel_clock, OPENGAT_INK);
}

struct opengat_panel_hit opengat_panel_hit(struct opengat_rect screen,
    uint32_t x, uint32_t y)
{
    struct opengat_panel_hit hit = { OPENGAT_PANEL_HIT_NONE, 0U };
    struct opengat_rect box;
    uint32_t at;

    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_MENU, &box) ==
            OPENGAT_PANEL_STATUS_OK &&
            opengat_rect_contains(box, x, y)) {
        hit.kind = OPENGAT_PANEL_HIT_MENU;
        return hit;
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_LAUNCHBAR,
            &box) == OPENGAT_PANEL_STATUS_OK &&
            opengat_rect_contains(box, x, y)) {
        hit.kind = OPENGAT_PANEL_HIT_LAUNCHER;
        hit.index = (x - box.x) / OPENGAT_BUTTON;
        return hit;
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_WINCMD,
            &box) == OPENGAT_PANEL_STATUS_OK &&
            opengat_rect_contains(box, x, y)) {
        hit.kind = OPENGAT_PANEL_HIT_WINCMD;
        return hit;
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_PAGER,
            &box) == OPENGAT_PANEL_STATUS_OK &&
            opengat_rect_contains(box, x, y)) {
        const uint32_t from_left = x - box.x;
        const uint32_t cell = from_left > OPENGAT_PAGER_INSET ?
            (from_left - OPENGAT_PAGER_INSET) / OPENGAT_PAGER_CELL : 0U;

        if (panel_desktops == 0U) {
            return hit;
        }
        hit.kind = OPENGAT_PANEL_HIT_PAGER;
        hit.index = cell < panel_desktops ? cell : panel_desktops - 1U;
        return hit;
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_TASKBAR,
            &box) == OPENGAT_PANEL_STATUS_OK &&
            opengat_rect_contains(box, x, y)) {
        uint32_t live = opengat_panel_task_count();
        uint32_t drawn = 0U;

        for (at = 0U; at < OPENGAT_PANEL_MAX_TASKS; ++at) {
            struct opengat_rect button;

            if (!panel_task_used[at] ||
                    panel_tasks[at].desktop != panel_desktop) {
                continue;
            }
            if (task_button(box, drawn, live, &button) &&
                    opengat_rect_contains(button, x, y)) {
                hit.kind = OPENGAT_PANEL_HIT_TASK;
                hit.index = at;
                return hit;
            }
            ++drawn;
        }
        return hit;
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_VOLUME,
            &box) == OPENGAT_PANEL_STATUS_OK &&
            opengat_rect_contains(box, x, y)) {
        hit.kind = OPENGAT_PANEL_HIT_VOLUME;
        return hit;
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_CLOCK,
            &box) == OPENGAT_PANEL_STATUS_OK &&
            opengat_rect_contains(box, x, y)) {
        hit.kind = OPENGAT_PANEL_HIT_CLOCK;
        return hit;
    }
    return hit;
}

enum opengat_panel_status opengat_panel_draw(struct opengat_rect screen)
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
    struct opengat_rect bar = opengat_panel_bounds(screen);
    struct opengat_rect box;

    if (!panel_ready) {
        return OPENGAT_PANEL_STATUS_NOT_INITIALIZED;
    }
    if (canvas == NULL) {
        return OPENGAT_PANEL_STATUS_SURFACE_FAILURE;
    }
    if (bar.width < 320U) {
        return OPENGAT_PANEL_STATUS_UNSUPPORTED_GEOMETRY;
    }
    draw_ground(bar);

    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_MENU, &box) ==
            OPENGAT_PANEL_STATUS_OK) {
        draw_mark(box, OPENGAT_ICON,
                  box.x + OPENGAT_BUTTON_PAD, middle(box, OPENGAT_ICON));
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_LAUNCHBAR,
            &box) == OPENGAT_PANEL_STATUS_OK) {
        draw_launchbar(box, LEFT_LAUNCHERS, 3U);
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_WINCMD,
            &box) == OPENGAT_PANEL_STATUS_OK) {
        draw_icon(box, "wincmd", OPENGAT_ICON,
                  box.x + OPENGAT_BUTTON_PAD, middle(box, OPENGAT_ICON));
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_PAGER,
            &box) == OPENGAT_PANEL_STATUS_OK) {
        draw_pager(box);
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_TASKBAR,
            &box) == OPENGAT_PANEL_STATUS_OK) {
        draw_tasks(box);
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_CPU,
            &box) == OPENGAT_PANEL_STATUS_OK) {
        draw_cpu(box);
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_VOLUME,
            &box) == OPENGAT_PANEL_STATUS_OK) {
        draw_icon(box,
                  (panel_muted || panel_volume == 0U) ?
                      "volume-muted" : "volume", OPENGAT_ICON,
                  box.x + OPENGAT_BUTTON_PAD, middle(box, OPENGAT_ICON));
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_TRAY,
            &box) == OPENGAT_PANEL_STATUS_OK) {
        draw_icon(box, "network", OPENGAT_ICON,
                  box.x + OPENGAT_BUTTON_PAD, middle(box, OPENGAT_ICON));
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_CLOCK,
            &box) == OPENGAT_PANEL_STATUS_OK) {
        draw_clock(box);
    }
    if (opengat_panel_plugin_bounds(screen,
            OPENGAT_PANEL_PLUGIN_LAUNCHBAR_RIGHT, &box) ==
            OPENGAT_PANEL_STATUS_OK) {
        draw_launchbar(box, RIGHT_LAUNCHERS, 0U);
    }
    return OPENGAT_PANEL_STATUS_OK;
}

/* =================================================================== API */

enum opengat_panel_status opengat_panel_attach(struct opengat_surface *surface)
{
    if (surface == NULL) {
        return OPENGAT_PANEL_STATUS_NULL_ARGUMENT;
    }
    canvas = surface;
    return OPENGAT_PANEL_STATUS_OK;
}

enum opengat_panel_status opengat_panel_initialize(void)
{
    uint32_t at;

    for (at = 0U; at < OPENGAT_PANEL_MAX_TASKS; ++at) {
        panel_task_used[at] = false;
    }
    for (at = 0U; at < OPENGAT_PANEL_CPU_COLUMNS; ++at) {
        panel_cpu[at] = 0U;
    }
    copy_label(panel_clock, "00:00", sizeof(panel_clock));
    panel_volume = 65U;
    panel_muted = false;
    panel_desktop = 0U;
    panel_desktops = 2U;
    panel_ready = true;
    return OPENGAT_PANEL_STATUS_OK;
}

bool opengat_panel_is_initialized(void)
{
    return panel_ready;
}

enum opengat_panel_status opengat_panel_set_task(
    uint32_t slot, const struct opengat_panel_task *task)
{
    if (task == NULL) {
        return OPENGAT_PANEL_STATUS_NULL_ARGUMENT;
    }
    if (!panel_ready) {
        return OPENGAT_PANEL_STATUS_NOT_INITIALIZED;
    }
    if (slot >= OPENGAT_PANEL_MAX_TASKS) {
        return OPENGAT_PANEL_STATUS_BAD_INDEX;
    }
    panel_tasks[slot] = *task;
    copy_label(panel_tasks[slot].label, task->label,
               OPENGAT_PANEL_LABEL_BYTES);
    panel_task_used[slot] = true;
    return OPENGAT_PANEL_STATUS_OK;
}

enum opengat_panel_status opengat_panel_clear_task(uint32_t slot)
{
    if (!panel_ready) {
        return OPENGAT_PANEL_STATUS_NOT_INITIALIZED;
    }
    if (slot >= OPENGAT_PANEL_MAX_TASKS) {
        return OPENGAT_PANEL_STATUS_BAD_INDEX;
    }
    panel_task_used[slot] = false;
    return OPENGAT_PANEL_STATUS_OK;
}

uint32_t opengat_panel_task_count(void)
{
    uint32_t count = 0U;
    uint32_t at;

    for (at = 0U; at < OPENGAT_PANEL_MAX_TASKS; ++at) {
        if (panel_task_used[at] &&
                panel_tasks[at].desktop == panel_desktop) {
            ++count;
        }
    }
    return count;
}

enum opengat_panel_status opengat_panel_push_cpu(uint32_t percent)
{
    uint32_t at;

    if (!panel_ready) {
        return OPENGAT_PANEL_STATUS_NOT_INITIALIZED;
    }
    for (at = 1U; at < OPENGAT_PANEL_CPU_COLUMNS; ++at) {
        panel_cpu[at - 1U] = panel_cpu[at];
    }
    panel_cpu[OPENGAT_PANEL_CPU_COLUMNS - 1U] = clamp_u32(percent, 100U);
    return OPENGAT_PANEL_STATUS_OK;
}

enum opengat_panel_status opengat_panel_set_clock(const char *text)
{
    if (text == NULL) {
        return OPENGAT_PANEL_STATUS_NULL_ARGUMENT;
    }
    if (!panel_ready) {
        return OPENGAT_PANEL_STATUS_NOT_INITIALIZED;
    }
    copy_label(panel_clock, text, sizeof(panel_clock));
    return OPENGAT_PANEL_STATUS_OK;
}

enum opengat_panel_status opengat_panel_set_volume(uint32_t level, bool muted)
{
    if (!panel_ready) {
        return OPENGAT_PANEL_STATUS_NOT_INITIALIZED;
    }
    panel_volume = clamp_u32(level, 100U);
    panel_muted = muted;
    return OPENGAT_PANEL_STATUS_OK;
}

enum opengat_panel_status opengat_panel_set_desktop(uint32_t current,
    uint32_t count)
{
    if (!panel_ready) {
        return OPENGAT_PANEL_STATUS_NOT_INITIALIZED;
    }
    if (count == 0U || count > OPENGAT_PANEL_MAX_DESKTOPS ||
            current >= count) {
        return OPENGAT_PANEL_STATUS_BAD_INDEX;
    }
    panel_desktop = current;
    panel_desktops = count;
    return OPENGAT_PANEL_STATUS_OK;
}

const char *opengat_panel_status_string(enum opengat_panel_status status)
{
    switch (status) {
    case OPENGAT_PANEL_STATUS_OK:
        return "ok";
    case OPENGAT_PANEL_STATUS_NULL_ARGUMENT:
        return "null argument";
    case OPENGAT_PANEL_STATUS_NOT_INITIALIZED:
        return "not initialized";
    case OPENGAT_PANEL_STATUS_BAD_INDEX:
        return "bad index";
    case OPENGAT_PANEL_STATUS_UNSUPPORTED_GEOMETRY:
        return "unsupported geometry";
    case OPENGAT_PANEL_STATUS_SURFACE_FAILURE:
        return "surface failure";
    case OPENGAT_PANEL_STATUS_FONT_FAILURE:
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
bool opengat_panel_self_test(void)
{
    struct opengat_rect screen = { 0U, 0U, 1280U, 800U };
    struct opengat_rect clock_empty;
    struct opengat_rect clock_busy;
    struct opengat_rect pager;
    struct opengat_panel_hit hit;
    struct opengat_panel_task task;
    uint32_t at;

    if (opengat_panel_initialize() != OPENGAT_PANEL_STATUS_OK) {
        return false;
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_CLOCK,
            &clock_empty) != OPENGAT_PANEL_STATUS_OK) {
        return false;
    }
    for (at = 0U; at < 4U; ++at) {
        task.icon = "terminal";
        task.active = at == 0U;
        task.minimised = false;
        task.desktop = 0U;
        copy_label(task.label, "a window", OPENGAT_PANEL_LABEL_BYTES);
        if (opengat_panel_set_task(at, &task) != OPENGAT_PANEL_STATUS_OK) {
            return false;
        }
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_CLOCK,
            &clock_busy) != OPENGAT_PANEL_STATUS_OK) {
        return false;
    }
    if (clock_empty.x != clock_busy.x) {
        return false;
    }
    if (string_length(panel_clock) == 0U) {
        return false;
    }
    if (opengat_panel_plugin_bounds(screen, OPENGAT_PANEL_PLUGIN_PAGER,
            &pager) != OPENGAT_PANEL_STATUS_OK) {
        return false;
    }
    hit = opengat_panel_hit(screen, pager.x, pager.y);
    if (hit.kind != OPENGAT_PANEL_HIT_PAGER || hit.index != 0U) {
        return false;
    }
    return true;
}
