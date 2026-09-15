/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_DE_SETTINGS_H
#define TRAIT_DE_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include <trait/de/surface.h>
#include <trait/de/window.h>

/*
 * Trait OS settings, grouped into three pages: lxappearance for the look,
 * pcmanfm's Desktop Preferences for the desktop, and lxpanel's Panel
 * Preferences for the bar.  They are one GTK notebook here, a page per
 * program, because three windows to change three things on one desktop is
 * three windows.
 *
 * EVERY CONTROL ON IT CHANGES SOMETHING.  A settings window whose
 * switches do nothing is the largest possible version of a control that
 * does not do what it is drawn as, so a setting this shell cannot carry
 * out is not offered - and where lxappearance lists a dozen themes that
 * are not installed, this lists the ones that are.
 */

#define TRAIT_SETTINGS_MAX_PAGES 6U
#define TRAIT_SETTINGS_MAX_ROWS 8U
#define TRAIT_SETTINGS_TEXT_BYTES 40U

enum trait_settings_kind {
    TRAIT_SETTINGS_CHOICE = 0,   /* one of a list, the current one lit */
    TRAIT_SETTINGS_SWITCH,       /* on or off */
    TRAIT_SETTINGS_NOTE          /* text, changing nothing and saying so */
};

/*
 * WHAT A ROW ACTUALLY CHANGES.
 *
 * A row used to carry a label and a value and nothing else, which made
 * the whole window a picture: it listed themes it could not apply and
 * offered switches that switched nothing.  `setting` says what the row
 * IS, so pressing it can do the thing rather than look like it did.
 */
enum trait_settings_what {
    TRAIT_SET_NOTHING = 0,
    TRAIT_SET_WIDGET_THEME,
    TRAIT_SET_DESKTOP_ICONS,
    TRAIT_SET_SHOW_HIDDEN,
    TRAIT_SET_FILES_VIEW
};

struct trait_settings_row {
    char label[TRAIT_SETTINGS_TEXT_BYTES];
    char value[TRAIT_SETTINGS_TEXT_BYTES];
    enum trait_settings_kind kind;
    bool on;
    enum trait_settings_what setting;
};

/*
 * Press a row.  A CHOICE steps to its next option and wraps; a SWITCH
 * flips.  Returns true if something changed, and the change is real -
 * picking a widget theme repaints every window on the desktop.
 */
bool trait_settings_press(uint32_t page, uint32_t row);
/* Where a row sits, so a press can be turned into one. */
bool trait_settings_row_bounds(const struct trait_window *window,
    uint32_t row, struct trait_rect *out);

struct trait_settings_page {
    char name[TRAIT_SETTINGS_TEXT_BYTES];
    struct trait_settings_row rows[TRAIT_SETTINGS_MAX_ROWS];
    uint32_t row_count;
};

void trait_settings_reset(void);
bool trait_settings_add_page(const char *name);
bool trait_settings_add_row(uint32_t page,
    const struct trait_settings_row *row);
uint32_t trait_settings_page_count(void);

void trait_settings_select(uint32_t page);
uint32_t trait_settings_selected(void);

bool trait_settings_tab_bounds(const struct trait_window *window,
    uint32_t page, struct trait_rect *out);

void trait_settings_draw(struct trait_surface *surface,
    const struct trait_window *window);

bool trait_settings_self_test(void);

#endif /* TRAIT_DE_SETTINGS_H */
