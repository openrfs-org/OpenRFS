/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd_desktop/menu.h>

#include <rsd_desktop/font.h>
#include <rsd_desktop/oxygen.h>
#include <rsd_desktop/theme.h>
#include <rsd_desktop/window.h>

#include "rsd_glass.h"

#define MENU_ROW 20U
#define MENU_RULE 7U
#define MENU_WIDTH 168U
#define MENU_PAD 8U

static struct rsd_menu_row rows[RSD_MENU_MAX_ROWS];
static uint32_t row_count;

static void copy(char *out, const char *text, uint32_t capacity)
{
    uint32_t at = 0U;

    while (text != NULL && text[at] != '\0' && at + 1U < capacity) {
        out[at] = text[at];
        ++at;
    }
    out[at] = '\0';
}

void rsd_menu_reset(void)
{
    row_count = 0U;
}

bool rsd_menu_add(const char *label, bool category, bool rule)
{
    if (row_count >= RSD_MENU_MAX_ROWS) {
        return false;
    }
    copy(rows[row_count].label, rule ? "" : label, RSD_MENU_TEXT_BYTES);
    rows[row_count].category = category;
    rows[row_count].rule = rule;
    ++row_count;
    return true;
}

uint32_t rsd_menu_row_count(void)
{
    return row_count;
}

bool rsd_menu_row_is_rule(uint32_t at)
{
    if (at >= row_count) {
        return false;
    }
    return rows[at].rule;
}

const char *rsd_menu_row_label(uint32_t at)
{
    if (at >= row_count || rows[at].rule) {
        return NULL;
    }
    return rows[at].label;
}

static uint32_t menu_height(void)
{
    uint32_t total = 4U + RSD_MENU_TITLE_HEIGHT;
    uint32_t at;

    for (at = 0U; at < row_count; ++at) {
        total += rows[at].rule ? MENU_RULE : MENU_ROW;
    }
    return total + 4U;
}

struct rsd_rect rsd_menu_bounds(struct rsd_rect screen,
    struct rsd_rect button)
{
    struct rsd_rect box;
    uint32_t height = menu_height();

    box.x = button.x;
    box.width = MENU_WIDTH;
    box.height = height;
    /*
     * UPWARDS: the menu's BOTTOM is the button's top, so it grows away
     * from the panel.  Laying it out downwards from a bottom panel is
     * how a menu ends up off the screen.
     */
    box.y = button.y > height ? button.y - height : 0U;
    if (box.x + box.width > screen.x + screen.width) {
        box.x = screen.x + screen.width - box.width;
    }
    return box;
}

void rsd_menu_draw(struct rsd_surface *surface,
    struct rsd_rect screen, struct rsd_rect button)
{
    struct rsd_rect box = rsd_menu_bounds(screen, button);
    uint32_t top = box.y + RSD_MENU_TITLE_HEIGHT + 4U;
    uint32_t at;
    uint32_t edge;

    if (!rsd_surface_valid(surface) || row_count == 0U) {
        return;
    }
    /*
     * AN OXYGEN MENU, WHICH IS A PIECE OF THE SAME GLASS.
     *
     * It was an fvwm menu - a raised slab with a bevel round it, a
     * groove under its title and a groove for every separator - because
     * OpenBSD's system.fvwmrc sets one in a line:
     *
     *     MenuStyle #4d4d4d #bebebe #e7e7e7 <times bold 12> fvwm
     *
     * That is the right menu for the shell this was copying and the
     * wrong one now, and the difference is not decoration. A bevel says
     * the menu is a RAISED PANEL: four grey lines standing proud of the
     * desktop. Oxygen says it is a sheet of the same lit glass as
     * everything else, floating, with a shadow under it and nothing
     * raised anywhere.
     *
     * So: the window gradient, the rounding a decoration gets, the one
     * dark line with the one light line inside it, the pool of light on
     * the top edge, and a shadow cast on whatever is behind. No bevel
     * and no grooves.
     */
    rsd_oxy_cast(surface, box, false);
    rsd_oxy_sheet(surface, box, box, RSD_BG, RSD_OXY_RADIUS, false);
    {
        uint32_t x;
        uint32_t y;

        for (y = box.y + 1U; y < box.y + RSD_GLASS_POOL_HIGH
                && y + 1U < box.y + box.height; ++y) {
            for (x = box.x + 2U; x + 2U < box.x + box.width; ++x) {
                rsd_surface_plot(surface, box, x, y,
                    rsd_oxy_pool(RSD_BG,
                                   rsd_surface_read(surface, x, y),
                                   x, y, box));
            }
        }
    }
    {
        /*
         * THE TITLE. A KDE menu has none; this is the root menu's own
         * name, which is fvwm's idea and worth keeping because it says
         * which menu you are looking at. What went is the groove under
         * it - Oxygen separates a heading from what follows with air,
         * not with a cut line.
         */
        struct rsd_rect head_box = box;
        uint32_t width = rsd_font_width(RSD_MENU_TITLE);

        head_box.height = RSD_MENU_TITLE_HEIGHT;
        rsd_font_draw(surface, head_box,
            head_box.x + (head_box.width > width ?
                (head_box.width - width) / 2U : 0U),
            head_box.y + (head_box.height + rsd_font_ascent()) / 2U - 1U,
            RSD_MENU_TITLE, RSD_FRAME_INK_DIM);
    }
    for (at = 0U; at < row_count; ++at) {
        if (rows[at].rule) {
            /* One line that FADES at both ends, which is what Oxygen
             * puts between groups. A groove - a dark line with a light
             * one under it - is a cut in a raised panel, and there is no
             * raised panel here any more. */
            uint32_t from = box.x + MENU_PAD;
            uint32_t to = box.x + box.width - MENU_PAD;
            uint32_t line = top + MENU_RULE / 2U;
            uint32_t along;

            for (along = 0U; from + along < to; ++along) {
                uint32_t span = to - from;
                uint32_t fade = along * 2U < span ?
                    along * 2U : (span - along) * 2U;

                rsd_surface_plot(surface, box, from + along, line,
                    rsd_oxy_mix(
                        rsd_surface_read(surface, from + along, line),
                        rsd_oxy_dark(RSD_BG), fade * 255U / span));
            }
            top += MENU_RULE;
            continue;
        }
        rsd_font_draw(surface, box, box.x + MENU_PAD,
                        top + (MENU_ROW + rsd_font_ascent()) / 2U - 1U,
                        rows[at].label, RSD_FG);
        if (rows[at].category) {
            /* The submenu arrow, pointing the way the submenu opens. */
            uint32_t tip = box.x + box.width - 12U;
            uint32_t mid = top + MENU_ROW / 2U;

            for (edge = 0U; edge < 4U; ++edge) {
                uint32_t span;

                for (span = 0U; span + edge < 4U; ++span) {
                    rsd_surface_plot(surface, box, tip + edge,
                                       mid - span, RSD_FG);
                    rsd_surface_plot(surface, box, tip + edge,
                                       mid + span, RSD_FG);
                }
            }
        }
        top += MENU_ROW;
    }
}

/*
 * The self test asks the thing that is wrong the first time anybody
 * writes this: does the menu open UPWARDS - is its bottom edge at the
 * button's top - or does it run down over the panel and off the screen?
 */
bool rsd_menu_self_test(void)
{
    struct rsd_rect screen = { 0U, 0U, 1280U, 800U };
    struct rsd_rect button = { 2U, 774U, 22U, 26U };
    struct rsd_rect box;

    rsd_menu_reset();
    if (!rsd_menu_add("Accessories", true, false) ||
            !rsd_menu_add("System Tools", true, false) ||
            !rsd_menu_add(NULL, false, true) ||
            !rsd_menu_add("Run...", false, false)) {
        return false;
    }
    if (rsd_menu_row_count() != 4U) {
        return false;
    }
    box = rsd_menu_bounds(screen, button);
    /* Its foot sits on the button's top, not below it. */
    if (box.y + box.height != button.y) {
        return false;
    }
    if (box.y >= button.y) {
        return false;
    }
    /* A rule is shorter than a row, so adding one must not add a row's
     * worth of height. */
    {
        uint32_t with_rule = box.height;

        rsd_menu_reset();
        (void)rsd_menu_add("Accessories", true, false);
        (void)rsd_menu_add("System Tools", true, false);
        (void)rsd_menu_add("Run...", false, false);
        box = rsd_menu_bounds(screen, button);
        if (with_rule - box.height != MENU_RULE) {
            return false;
        }
    }
    rsd_menu_reset();
    return true;
}
