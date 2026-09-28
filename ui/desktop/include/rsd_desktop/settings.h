/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_SETTINGS_H
#define RSD_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd_desktop/surface.h>
#include <rsd_desktop/window.h>

/*
 * Settings, which on LXDE is three programs: lxappearance for the look,
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

#define RSD_SETTINGS_MAX_PAGES 6U
#define RSD_SETTINGS_MAX_ROWS 8U
#define RSD_SETTINGS_TEXT_BYTES 40U

enum rsd_settings_kind {
    RSD_SETTINGS_CHOICE = 0,   /* one of a list, the current one lit */
    RSD_SETTINGS_SWITCH,       /* on or off */
    RSD_SETTINGS_NOTE          /* text, changing nothing and saying so */
};

/*
 * WHAT A ROW ACTUALLY CHANGES.
 *
 * A row used to carry a label and a value and nothing else, which made
 * the whole window a picture: it listed themes it could not apply and
 * offered switches that switched nothing.  `setting` says what the row
 * IS, so pressing it can do the thing rather than look like it did.
 */
enum rsd_settings_what {
    RSD_SET_NOTHING = 0,
    RSD_SET_WIDGET_THEME,      /* lxappearance: the widget theme */
    RSD_SET_DESKTOP_ICONS,     /* pcmanfm: draw the desktop's icons */
    RSD_SET_SHOW_HIDDEN,       /* pcmanfm: show_hidden */
    RSD_SET_FILES_VIEW,        /* pcmanfm: view_mode */
    RSD_SET_SINGLE_CLICK,      /* pcmanfm: single_click */
    RSD_SET_TERM_SHEER         /* the terminal's ground, mixed */
};

struct rsd_settings_row {
    char label[RSD_SETTINGS_TEXT_BYTES];
    char value[RSD_SETTINGS_TEXT_BYTES];
    enum rsd_settings_kind kind;
    bool on;
    enum rsd_settings_what setting;
};

/*
 * Press a row.  A CHOICE steps to its next option and wraps; a SWITCH
 * flips.  Returns true if something changed, and the change is real -
 * picking a widget theme repaints every window on the desktop.
 */
bool rsd_settings_press(uint32_t page, uint32_t row);
/* Where a row sits, so a press can be turned into one. */
bool rsd_settings_row_bounds(const struct rsd_window *window,
    uint32_t row, struct rsd_rect *out);

struct rsd_settings_page {
    char name[RSD_SETTINGS_TEXT_BYTES];
    struct rsd_settings_row rows[RSD_SETTINGS_MAX_ROWS];
    uint32_t row_count;
};

void rsd_settings_reset(void);
bool rsd_settings_add_page(const char *name);
bool rsd_settings_add_row(uint32_t page,
    const struct rsd_settings_row *row);
uint32_t rsd_settings_page_count(void);

void rsd_settings_select(uint32_t page);
uint32_t rsd_settings_selected(void);

bool rsd_settings_tab_bounds(const struct rsd_window *window,
    uint32_t page, struct rsd_rect *out);

void rsd_settings_draw(struct rsd_surface *surface,
    const struct rsd_window *window);

bool rsd_settings_self_test(void);

#endif /* RSD_SETTINGS_H */
