/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_DE_SETTINGS_H
#define OPENGAT_DE_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include <opengat/de/surface.h>
#include <opengat/de/window.h>

/*
 * OpenGAT settings, grouped into three pages: lxappearance for the look,
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

#define OPENGAT_SETTINGS_MAX_PAGES 6U
#define OPENGAT_SETTINGS_MAX_ROWS 8U
#define OPENGAT_SETTINGS_TEXT_BYTES 40U

enum opengat_settings_kind {
    OPENGAT_SETTINGS_CHOICE = 0,   /* one of a list, the current one lit */
    OPENGAT_SETTINGS_SWITCH,       /* on or off */
    OPENGAT_SETTINGS_NOTE          /* text, changing nothing and saying so */
};

/*
 * WHAT A ROW ACTUALLY CHANGES.
 *
 * A row used to carry a label and a value and nothing else, which made
 * the whole window a picture: it listed themes it could not apply and
 * offered switches that switched nothing.  `setting` says what the row
 * IS, so pressing it can do the thing rather than look like it did.
 */
enum opengat_settings_what {
    OPENGAT_SET_NOTHING = 0,
    OPENGAT_SET_WIDGET_THEME,
    OPENGAT_SET_DESKTOP_ICONS,
    OPENGAT_SET_SHOW_HIDDEN,
    OPENGAT_SET_FILES_VIEW
};

struct opengat_settings_row {
    char label[OPENGAT_SETTINGS_TEXT_BYTES];
    char value[OPENGAT_SETTINGS_TEXT_BYTES];
    enum opengat_settings_kind kind;
    bool on;
    enum opengat_settings_what setting;
};

/*
 * Press a row.  A CHOICE steps to its next option and wraps; a SWITCH
 * flips.  Returns true if something changed, and the change is real -
 * picking a widget theme repaints every window on the desktop.
 */
bool opengat_settings_press(uint32_t page, uint32_t row);
/* Where a row sits, so a press can be turned into one. */
bool opengat_settings_row_bounds(const struct opengat_window *window,
    uint32_t row, struct opengat_rect *out);

struct opengat_settings_page {
    char name[OPENGAT_SETTINGS_TEXT_BYTES];
    struct opengat_settings_row rows[OPENGAT_SETTINGS_MAX_ROWS];
    uint32_t row_count;
};

void opengat_settings_reset(void);
bool opengat_settings_add_page(const char *name);
bool opengat_settings_add_row(uint32_t page,
    const struct opengat_settings_row *row);
uint32_t opengat_settings_page_count(void);

void opengat_settings_select(uint32_t page);
uint32_t opengat_settings_selected(void);

bool opengat_settings_tab_bounds(const struct opengat_window *window,
    uint32_t page, struct opengat_rect *out);

void opengat_settings_draw(struct opengat_surface *surface,
    const struct opengat_window *window);

bool opengat_settings_self_test(void);

#endif /* OPENGAT_DE_SETTINGS_H */
