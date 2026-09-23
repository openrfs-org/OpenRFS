/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/menu.h>

#include <trait/font.h>
#include <trait/theme.h>

#define MENU_ROW 20U
#define MENU_RULE 7U
#define MENU_WIDTH 168U
#define MENU_PAD 8U

static struct trait_menu_row rows[TRAIT_MENU_MAX_ROWS];
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

void trait_menu_reset(void)
{
    row_count = 0U;
}

bool trait_menu_add(const char *label, bool category, bool rule)
{
    if (row_count >= TRAIT_MENU_MAX_ROWS) {
        return false;
    }
    copy(rows[row_count].label, rule ? "" : label, TRAIT_MENU_TEXT_BYTES);
    rows[row_count].category = category;
    rows[row_count].rule = rule;
    ++row_count;
    return true;
}

uint32_t trait_menu_row_count(void)
{
    return row_count;
}

bool trait_menu_row_is_rule(uint32_t at)
{
    if (at >= row_count) {
        return false;
    }
    return rows[at].rule;
}

const char *trait_menu_row_label(uint32_t at)
{
    if (at >= row_count || rows[at].rule) {
        return NULL;
    }
    return rows[at].label;
}

static uint32_t menu_height(void)
{
    uint32_t total = 4U + TRAIT_MENU_TITLE_HEIGHT;
    uint32_t at;

    for (at = 0U; at < row_count; ++at) {
        total += rows[at].rule ? MENU_RULE : MENU_ROW;
    }
    return total + 4U;
}

struct trait_rect trait_menu_bounds(struct trait_rect screen,
    struct trait_rect button)
{
    struct trait_rect box;
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

/*
 * The same two-colour bevel the window frame is built out of, at the one
 * depth a menu uses.  See src/window.c: the colours are fvwm's, computed
 * by its own GetHilite() and GetShadow().
 */
static void relieve(struct trait_surface *surface, struct trait_rect clip,
    struct trait_rect box, uint32_t lit, uint32_t shade)
{
    uint32_t ring;

    for (ring = 0U; ring < 2U; ++ring) {
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

/*
 * A GROOVE, not a rule.  fvwm separates a menu with two lines - the dark
 * one above the light one - so the gap reads as cut into the slab rather
 * than drawn on it.  One black line across is a list's idiom; this is a
 * panel's, and an fvwm menu is a panel.
 */
static void groove(struct trait_surface *surface, struct trait_rect clip,
    uint32_t from, uint32_t to, uint32_t y)
{
    uint32_t x;

    for (x = from; x < to; ++x) {
        trait_surface_plot(surface, clip, x, y, TRAIT_BG_ACTIVE);
        trait_surface_plot(surface, clip, x, y + 1U, TRAIT_LINE_LIGHT);
    }
}

void trait_menu_draw(struct trait_surface *surface,
    struct trait_rect screen, struct trait_rect button)
{
    struct trait_rect box = trait_menu_bounds(screen, button);
    uint32_t top = box.y + TRAIT_MENU_TITLE_HEIGHT + 4U;
    uint32_t at;
    uint32_t edge;

    if (!trait_surface_valid(surface) || row_count == 0U) {
        return;
    }
    /*
     * AN FVWM MENU, which is a raised slab and nothing else.
     *
     * OpenBSD's system.fvwmrc sets it in one line:
     *
     *     MenuStyle #4d4d4d #bebebe #e7e7e7 <times bold 12> fvwm
     *
     * - a dark grey ink on a light grey ground, and the word `fvwm` at
     * the end choosing the look.  There is no coloured title bar on it;
     * the title is the same grey as everything else with a groove under
     * it.  This menu had the window frame's own colour across the top,
     * which is a panel menu's arrangement and not this one's.
     *
     * #bebebe is not among the sixteen colours this theme has, so the
     * ground is TRAIT_BG - #AAAAAA, the palette's light grey.  What
     * relieves it is white and the dark grey, and that is not a
     * substitution either: fvwm's own GetShadow(#bebebe) comes out
     * #5F5F5F, which is the palette's #555555 to within a rounding.
     */
    trait_surface_fill(surface, box, box, TRAIT_BG);
    relieve(surface, box, box, TRAIT_LINE_LIGHT, TRAIT_BG_ACTIVE);
    {
        struct trait_rect head = box;
        uint32_t width = trait_font_width(TRAIT_MENU_TITLE);

        head.height = TRAIT_MENU_TITLE_HEIGHT;
        trait_font_draw(surface, head,
            head.x + (head.width > width ?
                (head.width - width) / 2U : 0U),
            head.y + 15U, TRAIT_MENU_TITLE, TRAIT_FG);
        groove(surface, box, box.x + MENU_PAD,
               box.x + box.width - MENU_PAD, head.y + head.height);
    }
    for (at = 0U; at < row_count; ++at) {
        if (rows[at].rule) {
            groove(surface, box, box.x + MENU_PAD,
                   box.x + box.width - MENU_PAD, top + MENU_RULE / 2U);
            top += MENU_RULE;
            continue;
        }
        trait_font_draw(surface, box, box.x + MENU_PAD, top + 14U,
                        rows[at].label, TRAIT_FG);
        if (rows[at].category) {
            /* The submenu arrow, pointing the way the submenu opens. */
            uint32_t tip = box.x + box.width - 12U;
            uint32_t mid = top + MENU_ROW / 2U;

            for (edge = 0U; edge < 4U; ++edge) {
                uint32_t span;

                for (span = 0U; span + edge < 4U; ++span) {
                    trait_surface_plot(surface, box, tip + edge,
                                       mid - span, TRAIT_FG);
                    trait_surface_plot(surface, box, tip + edge,
                                       mid + span, TRAIT_FG);
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
bool trait_menu_self_test(void)
{
    struct trait_rect screen = { 0U, 0U, 1280U, 800U };
    struct trait_rect button = { 2U, 774U, 22U, 26U };
    struct trait_rect box;

    trait_menu_reset();
    if (!trait_menu_add("Accessories", true, false) ||
            !trait_menu_add("System Tools", true, false) ||
            !trait_menu_add(NULL, false, true) ||
            !trait_menu_add("Run...", false, false)) {
        return false;
    }
    if (trait_menu_row_count() != 4U) {
        return false;
    }
    box = trait_menu_bounds(screen, button);
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

        trait_menu_reset();
        (void)trait_menu_add("Accessories", true, false);
        (void)trait_menu_add("System Tools", true, false);
        (void)trait_menu_add("Run...", false, false);
        box = trait_menu_bounds(screen, button);
        if (with_rule - box.height != MENU_RULE) {
            return false;
        }
    }
    trait_menu_reset();
    return true;
}
