/* SPDX-License-Identifier: GPL-3.0-only */
#include <opengat/de/menu.h>

#include <opengat/de/font.h>
#include <opengat/de/theme.h>

#define MENU_ROW 20U
#define MENU_RULE 7U
#define MENU_WIDTH 168U
#define MENU_PAD 8U

static struct opengat_menu_row rows[OPENGAT_MENU_MAX_ROWS];
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

void opengat_menu_reset(void)
{
    row_count = 0U;
}

bool opengat_menu_add(const char *label, bool category, bool rule)
{
    if (row_count >= OPENGAT_MENU_MAX_ROWS) {
        return false;
    }
    copy(rows[row_count].label, rule ? "" : label, OPENGAT_MENU_TEXT_BYTES);
    rows[row_count].category = category;
    rows[row_count].rule = rule;
    ++row_count;
    return true;
}

uint32_t opengat_menu_row_count(void)
{
    return row_count;
}

bool opengat_menu_row_is_rule(uint32_t at)
{
    if (at >= row_count) {
        return false;
    }
    return rows[at].rule;
}

const char *opengat_menu_row_label(uint32_t at)
{
    if (at >= row_count || rows[at].rule) {
        return NULL;
    }
    return rows[at].label;
}

static uint32_t menu_height(void)
{
    uint32_t total = 4U;
    uint32_t at;

    for (at = 0U; at < row_count; ++at) {
        total += rows[at].rule ? MENU_RULE : MENU_ROW;
    }
    return total + 4U;
}

struct opengat_rect opengat_menu_bounds(struct opengat_rect screen,
    struct opengat_rect button)
{
    struct opengat_rect box;
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

void opengat_menu_draw(struct opengat_surface *surface,
    struct opengat_rect screen, struct opengat_rect button)
{
    struct opengat_rect box = opengat_menu_bounds(screen, button);
    uint32_t top = box.y + 4U;
    uint32_t at;
    uint32_t edge;

    if (!opengat_surface_valid(surface) || row_count == 0U) {
        return;
    }
    opengat_surface_fill(surface, box, box, OPENGAT_BG);
    for (edge = 0U; edge < box.width; ++edge) {
        opengat_surface_plot(surface, box, box.x + edge, box.y, OPENGAT_LINE);
        opengat_surface_plot(surface, box, box.x + edge,
                           box.y + box.height - 1U, OPENGAT_LINE);
    }
    for (edge = 0U; edge < box.height; ++edge) {
        opengat_surface_plot(surface, box, box.x, box.y + edge, OPENGAT_LINE);
        opengat_surface_plot(surface, box, box.x + box.width - 1U,
                           box.y + edge, OPENGAT_LINE);
    }
    for (at = 0U; at < row_count; ++at) {
        if (rows[at].rule) {
            for (edge = MENU_PAD; edge + MENU_PAD < box.width; ++edge) {
                opengat_surface_plot(surface, box, box.x + edge,
                                   top + MENU_RULE / 2U, OPENGAT_LINE);
            }
            top += MENU_RULE;
            continue;
        }
        opengat_font_draw(surface, box, box.x + MENU_PAD, top + 14U,
                        rows[at].label, OPENGAT_FG);
        if (rows[at].category) {
            /* The submenu arrow, pointing the way the submenu opens. */
            uint32_t tip = box.x + box.width - 12U;
            uint32_t mid = top + MENU_ROW / 2U;

            for (edge = 0U; edge < 4U; ++edge) {
                uint32_t span;

                for (span = 0U; span + edge < 4U; ++span) {
                    opengat_surface_plot(surface, box, tip + edge,
                                       mid - span, OPENGAT_FG);
                    opengat_surface_plot(surface, box, tip + edge,
                                       mid + span, OPENGAT_FG);
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
bool opengat_menu_self_test(void)
{
    struct opengat_rect screen = { 0U, 0U, 1280U, 800U };
    struct opengat_rect button = { 2U, 774U, 22U, 26U };
    struct opengat_rect box;

    opengat_menu_reset();
    if (!opengat_menu_add("Accessories", true, false) ||
            !opengat_menu_add("System Tools", true, false) ||
            !opengat_menu_add(NULL, false, true) ||
            !opengat_menu_add("Run...", false, false)) {
        return false;
    }
    if (opengat_menu_row_count() != 4U) {
        return false;
    }
    box = opengat_menu_bounds(screen, button);
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

        opengat_menu_reset();
        (void)opengat_menu_add("Accessories", true, false);
        (void)opengat_menu_add("System Tools", true, false);
        (void)opengat_menu_add("Run...", false, false);
        box = opengat_menu_bounds(screen, button);
        if (with_rule - box.height != MENU_RULE) {
            return false;
        }
    }
    opengat_menu_reset();
    return true;
}
