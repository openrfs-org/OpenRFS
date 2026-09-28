/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_UI_H
#define RSD_UI_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd/line.h>
#include <rsd/term.h>

/*
 * The dialog toolkit.
 *
 * The same console, drawn at instead of written to: the shell appends
 * lines and lets the screen scroll, a dialog writes to fixed
 * coordinates. Both are 80x25 of cells.
 *
 * The look is bsdinstall's, which is dialog(1)'s: a blue field, a grey
 * box with a drop shadow, the title sitting in the top border, a list
 * you move through with the arrows, and a row of buttons in angle
 * brackets along the bottom. A lit letter is the key you can press
 * instead of walking the list, and the line along the bottom says what
 * the highlighted thing is for.
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
#define RSD_G_HLINE '\x80'
#define RSD_G_VLINE '\x81'
#define RSD_G_UL    '\x82'
#define RSD_G_UR    '\x83'
#define RSD_G_LL    '\x84'
#define RSD_G_LR    '\x85'
#define RSD_G_LTEE  '\x86'
#define RSD_G_RTEE  '\x87'
#define RSD_G_TTEE  '\x88'
#define RSD_G_BTEE  '\x89'
#define RSD_G_CROSS '\x8A'
#define RSD_G_SHADE '\x8B'
#define RSD_G_BLOCK '\x8C'
#define RSD_G_UP    '\x8D'
#define RSD_G_DOWN  '\x8E'

/* ----------------------------------------------------------- the paint */
/*
 * DIALOG(1)'S OWN COLOURS, out of dlg_colors.h - not a set chosen to
 * look about right.
 *
 *     SCREEN          fg CYAN   bg BLUE   bold
 *     SHADOW          fg BLACK  bg BLACK  bold
 *     DIALOG          fg BLACK  bg WHITE
 *     TITLE           fg BLUE   bg WHITE  bold
 *     BORDER          fg WHITE  bg WHITE  bold
 *     BORDER2         = DIALOG
 *     BUTTON_ACTIVE   fg WHITE  bg BLUE   bold
 *     BUTTON_INACTIVE fg BLACK  bg WHITE
 *     BUTTON_KEY_INACTIVE   fg RED     bg WHITE
 *     BUTTON_LABEL_ACTIVE   fg YELLOW  bg BLUE   bold
 *     ITEM            fg BLACK  bg WHITE
 *     ITEM_SELECTED   fg WHITE  bg BLUE   bold
 *     TAG_KEY         fg RED    bg WHITE
 *     GAUGE           fg BLUE   bg WHITE  bold
 *     FORM_ACTIVE_TEXT fg WHITE bg BLUE   bold
 *     FORM_TEXT        fg WHITE bg CYAN   bold
 *
 * On a sixteen-colour console curses' COLOR_WHITE without bold is the
 * light grey 0xAAAAAA, and bold sets bit 3 - so dialog's "white on
 * white, bold" border is bright white on grey, which is the lit top-left
 * edge of a raised box, and its BORDER2 is black on grey, the shaded
 * one. That pair is where the whole 3D look comes from.
 *
 * THE FIELD WAS RED. That was this project's one departure from
 * FreeBSD, on the grounds that the logo is red - and it made the
 * installer look like a warning. A bsdinstall field is blue, the
 * backtitle sits on it in bright cyan with a rule under it, and the box
 * is the only thing on the screen that is not blue. The red stays where
 * it belongs: on the hotkey letters, which is where dialog puts it too.
 */
#define RSD_C_FIELD     RSD_ATTR(RSD_HICYAN, RSD_BLUE)
#define RSD_C_BACKTITLE RSD_ATTR(RSD_HICYAN, RSD_BLUE)
#define RSD_C_HINT      RSD_ATTR(RSD_HICYAN, RSD_BLUE)
#define RSD_C_SHADOW    RSD_ATTR(RSD_DARK, RSD_BLACK)
#define RSD_C_BOX       RSD_ATTR(RSD_BLACK, RSD_GREY)
/* A raised edge: BORDER where the light is, BORDER2 where it is not. */
#define RSD_C_EDGE_HI   RSD_ATTR(RSD_WHITE, RSD_GREY)
#define RSD_C_EDGE_LO   RSD_ATTR(RSD_BLACK, RSD_GREY)
#define RSD_C_TITLE     RSD_ATTR(RSD_HIBLUE, RSD_GREY)
#define RSD_C_ITEM      RSD_ATTR(RSD_BLACK, RSD_GREY)
#define RSD_C_ITEM_SEL  RSD_ATTR(RSD_WHITE, RSD_BLUE)
#define RSD_C_KEY       RSD_ATTR(RSD_RED, RSD_GREY)
#define RSD_C_KEY_SEL   RSD_ATTR(RSD_YELLOW, RSD_BLUE)
#define RSD_C_INPUT     RSD_ATTR(RSD_WHITE, RSD_BLUE)
/* GAUGE is blue on white, and the filled part of the bar is that same
 * pair REVERSED - which is how dialog paints a bar without a second
 * colour and how the number on it changes side without moving. */
#define RSD_C_GAUGE     RSD_ATTR(RSD_HIBLUE, RSD_GREY)
#define RSD_C_GAUGE_ON  RSD_ATTR(RSD_GREY, RSD_HIBLUE)
#define RSD_C_WARN      RSD_ATTR(RSD_RED, RSD_GREY)
/* ITEMHELP: the only thing on the screen that is not on the field. */
#define RSD_C_ITEMHELP  RSD_ATTR(RSD_WHITE, RSD_BLACK)

/* ---------------------------------------------------------- the widget */

#define RSD_UI_ITEMS   20U
#define RSD_UI_TAG     18U
#define RSD_UI_DESC    52U
#define RSD_UI_HELP    76U
#define RSD_UI_FIELDS   4U
#define RSD_UI_VALUE   32U
#define RSD_UI_LABEL   18U
#define RSD_UI_BUTTONS  3U
#define RSD_UI_TITLE_MAX 44U
#define RSD_UI_TEXT_MAX 320U

enum rsd_ui_kind {
    RSD_UI_MENU = 0,   /* pick one and go on */
    RSD_UI_CHECK,      /* [ ] any number of them */
    RSD_UI_RADIO,      /* ( ) exactly one */
    RSD_UI_FORM,       /* labelled fields, some of them secret */
    RSD_UI_MSG,        /* something to read, and buttons */
    RSD_UI_GAUGE       /* a bar that fills while work happens */
};

enum rsd_ui_result {
    RSD_UI_EDITING = 0,  /* the key was taken, nothing settled */
    RSD_UI_ACCEPT,       /* a button that means go on was pressed */
    RSD_UI_REJECT,       /* cancel, or escape */
    RSD_UI_ASKED        /* F1: whoever owns this screen should explain */
};

struct rsd_ui_item {
    char tag[RSD_UI_TAG];
    char desc[RSD_UI_DESC];
    /* One line about the highlighted item, shown along the bottom of
     * the screen. sysinstall put it there and it is the difference
     * between a list of words and a list you can act on without
     * guessing. An item with nothing to add leaves it empty and the
     * key legend shows instead. */
    char help[RSD_UI_HELP];
    bool on;
    bool locked;   /* ticked, and the space bar will not untick it */
};

struct rsd_ui_field {
    char label[RSD_UI_LABEL];
    char value[RSD_UI_VALUE];
    uint32_t width;       /* how wide the box is drawn */
    bool secret;          /* typed but never shown */
};

#define RSD_UI_HLINE_MAX 48U

struct rsd_ui {
    enum rsd_ui_kind kind;
    char title[RSD_UI_TITLE_MAX];
    char text[RSD_UI_TEXT_MAX];
    /*
     * THE KEY LEGEND, in the box's BOTTOM border - dialog(1)'s --hline.
     *
     * Not a decoration and not a status line: it is the only place a
     * dialog says which keys move you, and bsdinstall changes it per
     * screen because a checklist is the only widget the space bar does
     * anything to.
     */
    char hline[RSD_UI_HLINE_MAX];

    struct rsd_ui_item item[RSD_UI_ITEMS];
    uint32_t items;
    uint32_t cursor;      /* which item the highlight is on */
    uint32_t top;         /* first item drawn, when the list is long */
    uint32_t visible;     /* how many rows of list to draw */

    struct rsd_ui_field field[RSD_UI_FIELDS];
    uint32_t fields;
    uint32_t focus;       /* which field is being typed into */
    uint32_t caret;       /* where in that field the next character goes */

    char button[RSD_UI_BUTTONS][RSD_UI_LABEL];
    uint32_t buttons;
    uint32_t chosen;      /* which button the highlight is on */
    bool on_buttons;      /* the highlight is down on the button row */

    uint32_t percent;     /* the gauge */
    char note[RSD_UI_DESC];  /* one line under the gauge, what it is doing */
};

/* Building one. The text is the paragraph above the widget; a widget
 * with nothing to explain passes NULL rather than an empty string. */
void rsd_ui_begin(struct rsd_ui *ui, enum rsd_ui_kind kind,
                   const char *title, const char *text);
void rsd_ui_item(struct rsd_ui *ui, const char *tag, const char *desc,
                  bool on);
void rsd_ui_field(struct rsd_ui *ui, const char *label, const char *value,
                   uint32_t width, bool secret);
void rsd_ui_buttons(struct rsd_ui *ui, const char *a, const char *b,
                     const char *c);
void rsd_ui_select(struct rsd_ui *ui, uint32_t index);
/* Some things are not optional, and a box you can untick that comes
 * back ticked is worse than one that will not move. */
void rsd_ui_lock(struct rsd_ui *ui, uint32_t index);
/* The bottom line for the item last added. */
void rsd_ui_help(struct rsd_ui *ui, const char *text);
/* The key legend in the bottom border. */
void rsd_ui_hline(struct rsd_ui *ui, const char *text);
/* What the bottom line should say right now: the highlighted item's
 * own line if it has one, otherwise NULL and the caller's legend. */
const char *rsd_ui_hint(const struct rsd_ui *ui);
void rsd_ui_gauge(struct rsd_ui *ui, uint32_t percent, const char *note);

/* Driving one. */
enum rsd_ui_result rsd_ui_key(struct rsd_ui *ui, int key);

/* Reading one back. */
const char *rsd_ui_tag(const struct rsd_ui *ui);
const char *rsd_ui_value(const struct rsd_ui *ui, uint32_t field);
const char *rsd_ui_button(const struct rsd_ui *ui);
bool rsd_ui_checked(const struct rsd_ui *ui, const char *tag);
uint32_t rsd_ui_checked_count(const struct rsd_ui *ui);

/* Drawing one. The screen behind it is painted first, every time: a
 * dialog that drew only itself would leave the last one showing. */
void rsd_ui_backdrop(struct rsd_term *t, const char *backtitle,
                      const char *hint);
void rsd_ui_draw(const struct rsd_ui *ui, struct rsd_term *t);

/*
 * WHAT RETURN WOULD TAKE: the highlighted item, the ticked ones, the
 * field, or the chosen button. One definition, so what a screen offers
 * and what it hands back cannot drift.
 *
 * THE TRANSCRIPT RENDERER THAT USED TO BE HERE IS GONE. rsd_ui_ask()
 * drew one of these as OpenBSD install(8) would have asked it - a
 * question at the bottom of a scrolling console, with the default in
 * square brackets - and it is what the installer was using while
 * rsd_ui_draw() sat unused beside it. bsdinstall is a dialog program;
 * this is a dialog toolkit; there is one renderer again.
 */
void rsd_ui_default(const struct rsd_ui *ui, char *out,
                     uint32_t capacity);

/* The pieces, which the installer's own screens also draw with. */
void rsd_ui_frame(struct rsd_term *t, uint32_t row, uint32_t col,
                   uint32_t rows, uint32_t cols, const char *title);
uint32_t rsd_ui_centre(uint32_t width, uint32_t cols);

#endif /* RSD_UI_H */
