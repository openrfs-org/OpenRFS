/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_DE_MENU_H
#define OPENGAT_DE_MENU_H

#include <stdbool.h>
#include <stdint.h>

#include <opengat/de/surface.h>

/*
 * The panel's applications menu.
 *
 * The profile's menu plugin lists its contents literally:
 *
 *     system { }  |  separator  |  item{command=run}
 *
 * `system` is the application menu, which LXDE builds from the .desktop
 * files on the machine and groups by their freedesktop category.  A
 * category with nothing in it is not drawn, because LXDE does not draw
 * one either.
 *
 * IT OPENS UPWARDS off the button, because the panel is at the foot of
 * the screen - a menu that dropped down from a bottom panel would go off
 * the bottom of the display.
 *
 * WHAT IS IN IT IS NOT A CHOICE MADE HERE: an entry is a package, and a
 * package that is not installed is not in the menu.  That is what makes
 * the package manager a package manager rather than a shop window.
 */

#define OPENGAT_MENU_MAX_ROWS 16U
#define OPENGAT_MENU_TEXT_BYTES 32U

struct opengat_menu_row {
    char label[OPENGAT_MENU_TEXT_BYTES];
    bool category;     /* has a submenu arrow */
    bool rule;         /* a separator, and the label is ignored */
};

void opengat_menu_reset(void);
bool opengat_menu_add(const char *label, bool category, bool rule);
uint32_t opengat_menu_row_count(void);
/* What a row IS, so a hit test does not have to keep its own copy of the
 * list to know whether a given y landed on a rule or a command. */
bool opengat_menu_row_is_rule(uint32_t at);
const char *opengat_menu_row_label(uint32_t at);

/* Where the menu sits given the button it hangs off and the screen. */
struct opengat_rect opengat_menu_bounds(struct opengat_rect screen,
    struct opengat_rect button);

void opengat_menu_draw(struct opengat_surface *surface,
    struct opengat_rect screen, struct opengat_rect button);

bool opengat_menu_self_test(void);

#endif /* OPENGAT_DE_MENU_H */
