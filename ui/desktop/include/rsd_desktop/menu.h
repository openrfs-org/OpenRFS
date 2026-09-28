/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_MENU_H
#define RSD_MENU_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd_desktop/surface.h>

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

/*
 * THE MENU WEARS A TITLE, the way fvwm's does - "Root Menu" in the
 * frame's own colours across the top of it.  It is not a control and
 * nothing presses it; it is what tells you which menu this is when a
 * submenu is open beside it.
 */
#define RSD_MENU_TITLE_HEIGHT 18U
#define RSD_MENU_TITLE "Root Menu"

#define RSD_MENU_MAX_ROWS 16U
#define RSD_MENU_TEXT_BYTES 32U

struct rsd_menu_row {
    char label[RSD_MENU_TEXT_BYTES];
    bool category;     /* has a submenu arrow */
    bool rule;         /* a separator, and the label is ignored */
};

void rsd_menu_reset(void);
bool rsd_menu_add(const char *label, bool category, bool rule);
uint32_t rsd_menu_row_count(void);
/* What a row IS, so a hit test does not have to keep its own copy of the
 * list to know whether a given y landed on a rule or a command. */
bool rsd_menu_row_is_rule(uint32_t at);
const char *rsd_menu_row_label(uint32_t at);

/* Where the menu sits given the button it hangs off and the screen. */
struct rsd_rect rsd_menu_bounds(struct rsd_rect screen,
    struct rsd_rect button);

void rsd_menu_draw(struct rsd_surface *surface,
    struct rsd_rect screen, struct rsd_rect button);

bool rsd_menu_self_test(void);

#endif /* RSD_MENU_H */
