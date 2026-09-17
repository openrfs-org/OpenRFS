/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DE_SETTINGS_H
#define OPENRFS_DE_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include <openrfs/de/surface.h>
#include <openrfs/de/window.h>

/*
 * OpenRFS settings, grouped into three pages: lxappearance for the look,
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

#define OPENRFS_SETTINGS_MAX_PAGES 6U
#define OPENRFS_SETTINGS_MAX_ROWS 8U
#define OPENRFS_SETTINGS_TEXT_BYTES 40U

enum openrfs_settings_kind {
    OPENRFS_SETTINGS_CHOICE = 0,   /* one of a list, the current one lit */
    OPENRFS_SETTINGS_SWITCH,       /* on or off */
    OPENRFS_SETTINGS_NOTE          /* text, changing nothing and saying so */
};

/*
 * WHAT A ROW ACTUALLY CHANGES.
 *
 * A row used to carry a label and a value and nothing else, which made
 * the whole window a picture: it listed themes it could not apply and
 * offered switches that switched nothing.  `setting` says what the row
 * IS, so pressing it can do the thing rather than look like it did.
 */
enum openrfs_settings_what {
    OPENRFS_SET_NOTHING = 0,
    OPENRFS_SET_WIDGET_THEME,
    OPENRFS_SET_DESKTOP_ICONS,
    OPENRFS_SET_SHOW_HIDDEN,
    OPENRFS_SET_FILES_VIEW
};

struct openrfs_settings_row {
    char label[OPENRFS_SETTINGS_TEXT_BYTES];
    char value[OPENRFS_SETTINGS_TEXT_BYTES];
    enum openrfs_settings_kind kind;
    bool on;
    enum openrfs_settings_what setting;
};

/*
 * Press a row.  A CHOICE steps to its next option and wraps; a SWITCH
 * flips.  Returns true if something changed, and the change is real -
 * picking a widget theme repaints every window on the desktop.
 */
bool openrfs_settings_press(uint32_t page, uint32_t row);
/* Where a row sits, so a press can be turned into one. */
bool openrfs_settings_row_bounds(const struct openrfs_window *window,
    uint32_t row, struct openrfs_rect *out);

struct openrfs_settings_page {
    char name[OPENRFS_SETTINGS_TEXT_BYTES];
    struct openrfs_settings_row rows[OPENRFS_SETTINGS_MAX_ROWS];
    uint32_t row_count;
};

void openrfs_settings_reset(void);
bool openrfs_settings_add_page(const char *name);
bool openrfs_settings_add_row(uint32_t page,
    const struct openrfs_settings_row *row);
uint32_t openrfs_settings_page_count(void);

void openrfs_settings_select(uint32_t page);
uint32_t openrfs_settings_selected(void);

bool openrfs_settings_tab_bounds(const struct openrfs_window *window,
    uint32_t page, struct openrfs_rect *out);

void openrfs_settings_draw(struct openrfs_surface *surface,
    const struct openrfs_window *window);

bool openrfs_settings_self_test(void);

#endif /* OPENRFS_DE_SETTINGS_H */
