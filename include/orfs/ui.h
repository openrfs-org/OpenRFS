/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ORFS_UI_H
#define ORFS_UI_H

#include <stdbool.h>
#include <stdint.h>

#include <orfs/line.h>
#include <orfs/term.h>

/*
 * The dialog toolkit.
 *
 * The same console, drawn at instead of written to: the shell appends
 * lines and lets the screen scroll, a dialog writes to fixed
 * coordinates. Both are 80x25 of cells.
 *
 * The look is sysinstall's and bsdinstall's: a
 * field, a grey box with a drop shadow, the title sitting in the top
 * border, a list you move through with the arrows, and a row of buttons
 * in angle brackets along the bottom. A lit letter is the key you can
 * press instead of walking the list, and the line along the bottom says
 * what the highlighted thing is for.
 *
 * The field is red rather than blue. That is the only thing about it
 * that is not FreeBSD's, and it is the same red the logo is drawn in.
 *
 * Nothing here blocks. A widget is a struct you hand one key at a
 * time; it returns whether it is finished. That is what lets the tests
 * drive it, since a widget with its own input loop needs a person at a
 * keyboard.
 */

/* ---------------------------------------------------------- the glyphs */
/*
 * Above ASCII, which is why they are named here rather than written as
 * numbers in drawing code. tools/make-font.py draws these rather than
 * scaling them out of a typeface, so that a frame laid out of eighty of
 * them has no seams.
 */
#define ORFS_G_HLINE '\x80'
#define ORFS_G_VLINE '\x81'
#define ORFS_G_UL    '\x82'
#define ORFS_G_UR    '\x83'
#define ORFS_G_LL    '\x84'
#define ORFS_G_LR    '\x85'
#define ORFS_G_LTEE  '\x86'
#define ORFS_G_RTEE  '\x87'
#define ORFS_G_TTEE  '\x88'
#define ORFS_G_BTEE  '\x89'
#define ORFS_G_CROSS '\x8A'
#define ORFS_G_SHADE '\x8B'
#define ORFS_G_BLOCK '\x8C'
#define ORFS_G_UP    '\x8D'
#define ORFS_G_DOWN  '\x8E'

/* ----------------------------------------------------------- the paint */
/*
 * Fifteen decisions, in one place, so that no drawing function invents
 * a sixteenth. A box is grey because a box is grey, everywhere.
 */
#define ORFS_C_FIELD     ORFS_ATTR(ORFS_GREY, ORFS_RED)
#define ORFS_C_BACKTITLE ORFS_ATTR(ORFS_WHITE, ORFS_RED)
#define ORFS_C_HINT      ORFS_ATTR(ORFS_YELLOW, ORFS_RED)
#define ORFS_C_SHADOW    ORFS_ATTR(ORFS_DARK, ORFS_BLACK)
#define ORFS_C_BOX       ORFS_ATTR(ORFS_BLACK, ORFS_GREY)
/* A raised edge: light where the light is, dark where it is not. */
#define ORFS_C_EDGE_HI   ORFS_ATTR(ORFS_WHITE, ORFS_GREY)
#define ORFS_C_EDGE_LO   ORFS_ATTR(ORFS_DARK, ORFS_GREY)
#define ORFS_C_TITLE     ORFS_ATTR(ORFS_RED, ORFS_GREY)
#define ORFS_C_ITEM      ORFS_ATTR(ORFS_BLACK, ORFS_GREY)
#define ORFS_C_ITEM_SEL  ORFS_ATTR(ORFS_WHITE, ORFS_RED)
#define ORFS_C_KEY       ORFS_ATTR(ORFS_RED, ORFS_GREY)
#define ORFS_C_KEY_SEL   ORFS_ATTR(ORFS_YELLOW, ORFS_RED)
#define ORFS_C_INPUT     ORFS_ATTR(ORFS_WHITE, ORFS_RED)
#define ORFS_C_GAUGE     ORFS_ATTR(ORFS_RED, ORFS_GREY)
#define ORFS_C_WARN      ORFS_ATTR(ORFS_RED, ORFS_GREY)

/* ---------------------------------------------------------- the widget */

#define ORFS_UI_ITEMS   20U
#define ORFS_UI_TAG     18U
#define ORFS_UI_DESC    52U
#define ORFS_UI_HELP    76U
#define ORFS_UI_FIELDS   4U
#define ORFS_UI_VALUE   32U
#define ORFS_UI_LABEL   18U
#define ORFS_UI_BUTTONS  3U
#define ORFS_UI_TITLE_MAX 44U
#define ORFS_UI_TEXT_MAX 320U

enum orfs_ui_kind {
    ORFS_UI_MENU = 0,   /* pick one and go on */
    ORFS_UI_CHECK,      /* [ ] any number of them */
    ORFS_UI_RADIO,      /* ( ) exactly one */
    ORFS_UI_FORM,       /* labelled fields, some of them secret */
    ORFS_UI_MSG,        /* something to read, and buttons */
    ORFS_UI_GAUGE       /* a bar that fills while work happens */
};

enum orfs_ui_result {
    ORFS_UI_EDITING = 0,  /* the key was taken, nothing settled */
    ORFS_UI_ACCEPT,       /* a button that means go on was pressed */
    ORFS_UI_REJECT,       /* cancel, or escape */
    ORFS_UI_ASKED        /* F1: whoever owns this screen should explain */
};

struct orfs_ui_item {
    char tag[ORFS_UI_TAG];
    char desc[ORFS_UI_DESC];
    /* One line about the highlighted item, shown along the bottom of
     * the screen. sysinstall put it there and it is the difference
     * between a list of words and a list you can act on without
     * guessing. An item with nothing to add leaves it empty and the
     * key legend shows instead. */
    char help[ORFS_UI_HELP];
    bool on;
    bool locked;   /* ticked, and the space bar will not untick it */
};

struct orfs_ui_field {
    char label[ORFS_UI_LABEL];
    char value[ORFS_UI_VALUE];
    uint32_t width;       /* how wide the box is drawn */
    bool secret;          /* typed but never shown */
};

struct orfs_ui {
    enum orfs_ui_kind kind;
    char title[ORFS_UI_TITLE_MAX];
    char text[ORFS_UI_TEXT_MAX];

    struct orfs_ui_item item[ORFS_UI_ITEMS];
    uint32_t items;
    uint32_t cursor;      /* which item the highlight is on */
    uint32_t top;         /* first item drawn, when the list is long */
    uint32_t visible;     /* how many rows of list to draw */

    struct orfs_ui_field field[ORFS_UI_FIELDS];
    uint32_t fields;
    uint32_t focus;       /* which field is being typed into */
    uint32_t caret;       /* where in that field the next character goes */

    char button[ORFS_UI_BUTTONS][ORFS_UI_LABEL];
    uint32_t buttons;
    uint32_t chosen;      /* which button the highlight is on */
    bool on_buttons;      /* the highlight is down on the button row */

    uint32_t percent;     /* the gauge */
    char note[ORFS_UI_DESC];  /* one line under the gauge, what it is doing */
};

/* Building one. The text is the paragraph above the widget; a widget
 * with nothing to explain passes NULL rather than an empty string. */
void orfs_ui_begin(struct orfs_ui *ui, enum orfs_ui_kind kind,
                   const char *title, const char *text);
void orfs_ui_item(struct orfs_ui *ui, const char *tag, const char *desc,
                  bool on);
void orfs_ui_field(struct orfs_ui *ui, const char *label, const char *value,
                   uint32_t width, bool secret);
void orfs_ui_buttons(struct orfs_ui *ui, const char *a, const char *b,
                     const char *c);
void orfs_ui_select(struct orfs_ui *ui, uint32_t index);
/* Some things are not optional, and a box you can untick that comes
 * back ticked is worse than one that will not move. */
void orfs_ui_lock(struct orfs_ui *ui, uint32_t index);
/* The bottom line for the item last added. */
void orfs_ui_help(struct orfs_ui *ui, const char *text);
/* What the bottom line should say right now: the highlighted item's
 * own line if it has one, otherwise NULL and the caller's legend. */
const char *orfs_ui_hint(const struct orfs_ui *ui);
void orfs_ui_gauge(struct orfs_ui *ui, uint32_t percent, const char *note);

/* Driving one. */
enum orfs_ui_result orfs_ui_key(struct orfs_ui *ui, int key);

/* Reading one back. */
const char *orfs_ui_tag(const struct orfs_ui *ui);
const char *orfs_ui_value(const struct orfs_ui *ui, uint32_t field);
const char *orfs_ui_button(const struct orfs_ui *ui);
bool orfs_ui_checked(const struct orfs_ui *ui, const char *tag);
uint32_t orfs_ui_checked_count(const struct orfs_ui *ui);

/* Drawing one. The screen behind it is painted first, every time: a
 * dialog that drew only itself would leave the last one showing. */
void orfs_ui_backdrop(struct orfs_term *t, const char *backtitle,
                      const char *hint);
void orfs_ui_draw(const struct orfs_ui *ui, struct orfs_term *t);

/* The pieces, which the installer's own screens also draw with. */
void orfs_ui_frame(struct orfs_term *t, uint32_t row, uint32_t col,
                   uint32_t rows, uint32_t cols, const char *title);
uint32_t orfs_ui_centre(uint32_t width, uint32_t cols);

#endif /* ORFS_UI_H */
