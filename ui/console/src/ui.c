/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/ui.h>

/*
 * The layout, copied from dialog(1):
 *
 *      +- Title ---------------------+
 *      | the paragraph, wrapped      |
 *      |                             |
 *      |   the list, or the fields   |
 *      |                             |
 *      |      <  OK  >  <Cancel>     |
 *      +-----------------------------+
 *
 * Its size comes from what is in it - the longest item, the widest
 * field, the button row - and then it is centred. Nothing is positioned
 * by hand, because a box positioned by hand is a box that is wrong the
 * first time somebody adds a longer word to it.
 */

#define BOX_MIN 42U
#define BOX_MAX 74U
#define PAD 2U           /* border plus one space, each side */

/* ------------------------------------------------------------ helpers */

static uint32_t umax(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static uint32_t umin(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

uint32_t rsd_ui_centre(uint32_t width, uint32_t cols)
{
    return width >= cols ? 0U : (cols - width) / 2U;
}

/*
 * Greedy word wrap. Returns how many lines the text needs at this
 * width, and if `want` names one of them, copies it out. Counting and
 * fetching are the same walk on purpose: a box whose height came from
 * one routine and whose text came from another is a box that one day
 * has four lines of room and five lines of text.
 */
static uint32_t wrap(const char *s, uint32_t width, uint32_t want,
                     char *out, uint32_t cap)
{
    uint32_t at = 0U;
    uint32_t line = 0U;

    if (out != NULL && cap != 0U) {
        out[0] = '\0';
    }
    if (s == NULL || s[0] == '\0' || width == 0U) {
        return 0U;
    }
    while (s[at] != '\0') {
        uint32_t start = at;
        uint32_t last_break = 0U;
        uint32_t taken = 0U;

        while (s[at] != '\0' && s[at] != '\n' && taken < width) {
            if (s[at] == ' ') {
                last_break = taken;
            }
            ++at;
            ++taken;
        }
        /* Mid-word at the edge: back up to the last space we passed. */
        if (s[at] != '\0' && s[at] != '\n' && s[at] != ' '
            && last_break != 0U) {
            at = start + last_break;
            taken = last_break;
        }
        if (line == want && out != NULL) {
            uint32_t n = umin(taken, cap != 0U ? cap - 1U : 0U);

            for (uint32_t i = 0U; i < n; ++i) {
                out[i] = s[start + i];
            }
            out[n] = '\0';
        }
        ++line;
        while (s[at] == ' ') {
            ++at;
        }
        if (s[at] == '\n') {
            ++at;
        }
    }
    return line;
}

/* Buttons are all drawn the same width, so the row is even. */
static uint32_t button_width(const struct rsd_ui *ui)
{
    uint32_t w = 0U;

    for (uint32_t i = 0U; i < ui->buttons; ++i) {
        w = umax(w, rsd_strlen(ui->button[i]));
    }
    /* Every button is as wide as the widest label, so the row is even,
     * and the brackets sit tight against the longest one: <Cancel> and
     * <  OK  > and <Cancel>, the way dialog(1) draws them. */
    return w + 2U;
}

static uint32_t button_row_width(const struct rsd_ui *ui)
{
    if (ui->buttons == 0U) {
        return 0U;
    }
    return ui->buttons * button_width(ui) + (ui->buttons - 1U) * 2U;
}

/* How wide an item is drawn, marker included. */
static uint32_t marker_width(const struct rsd_ui *ui)
{
    return (ui->kind == RSD_UI_CHECK || ui->kind == RSD_UI_RADIO)
           ? 4U : 0U;
}

static uint32_t tag_width(const struct rsd_ui *ui)
{
    uint32_t w = 0U;

    for (uint32_t i = 0U; i < ui->items; ++i) {
        w = umax(w, rsd_strlen(ui->item[i].tag));
    }
    return w;
}

static uint32_t item_width(const struct rsd_ui *ui)
{
    uint32_t desc = 0U;

    for (uint32_t i = 0U; i < ui->items; ++i) {
        desc = umax(desc, rsd_strlen(ui->item[i].desc));
    }
    return marker_width(ui) + tag_width(ui) + (desc != 0U ? 2U + desc : 0U);
}

static uint32_t label_width(const struct rsd_ui *ui)
{
    uint32_t w = 0U;

    for (uint32_t i = 0U; i < ui->fields; ++i) {
        w = umax(w, rsd_strlen(ui->field[i].label));
    }
    return w;
}

static uint32_t form_width(const struct rsd_ui *ui)
{
    uint32_t v = 0U;

    for (uint32_t i = 0U; i < ui->fields; ++i) {
        v = umax(v, ui->field[i].width);
    }
    return label_width(ui) + 1U + v;
}

/* ------------------------------------------------------------ building */

void rsd_ui_begin(struct rsd_ui *ui, enum rsd_ui_kind kind,
                   const char *title, const char *text)
{
    if (ui == NULL) {
        return;
    }
    for (uint32_t i = 0U; i < RSD_UI_ITEMS; ++i) {
        ui->item[i].tag[0] = '\0';
        ui->item[i].desc[0] = '\0';
        ui->item[i].help[0] = '\0';
        ui->item[i].on = false;
        ui->item[i].locked = false;
    }
    for (uint32_t i = 0U; i < RSD_UI_FIELDS; ++i) {
        ui->field[i].label[0] = '\0';
        ui->field[i].value[0] = '\0';
        ui->field[i].width = 0U;
        ui->field[i].secret = false;
    }
    for (uint32_t i = 0U; i < RSD_UI_BUTTONS; ++i) {
        ui->button[i][0] = '\0';
    }
    ui->kind = kind;
    (void)rsd_strcopy(ui->title, sizeof(ui->title), title);
    (void)rsd_strcopy(ui->text, sizeof(ui->text), text);
    ui->items = 0U;
    ui->cursor = 0U;
    ui->top = 0U;
    ui->visible = 0U;
    ui->fields = 0U;
    ui->focus = 0U;
    ui->caret = 0U;
    ui->buttons = 0U;
    ui->chosen = 0U;
    /* Something you only read has nowhere else for the highlight to be,
     * so it starts on the buttons. Anything with a list or a form
     * starts in the list, which is where the question is. */
    ui->on_buttons = kind == RSD_UI_MSG;
    ui->percent = 0U;
    ui->note[0] = '\0';
    /* Every dialog has somewhere to go and a way out of it. A screen
     * built without calling rsd_ui_buttons still has these. */
    rsd_ui_buttons(ui, "OK", "Cancel", NULL);
}

void rsd_ui_item(struct rsd_ui *ui, const char *tag, const char *desc,
                  bool on)
{
    struct rsd_ui_item *it;

    if (ui == NULL || ui->items >= RSD_UI_ITEMS) {
        return;
    }
    it = &ui->item[ui->items];
    (void)rsd_strcopy(it->tag, sizeof(it->tag), tag);
    (void)rsd_strcopy(it->desc, sizeof(it->desc), desc);
    it->help[0] = '\0';
    it->on = on;
    it->locked = false;
    ++ui->items;
    /* A list shows eight rows, or all of them if there are fewer. Past
     * eight it scrolls, and says so with an arrow. */
    ui->visible = umin(ui->items, 8U);
}

void rsd_ui_field(struct rsd_ui *ui, const char *label, const char *value,
                   uint32_t width, bool secret)
{
    struct rsd_ui_field *f;

    if (ui == NULL || ui->fields >= RSD_UI_FIELDS) {
        return;
    }
    f = &ui->field[ui->fields];
    (void)rsd_strcopy(f->label, sizeof(f->label), label);
    (void)rsd_strcopy(f->value, sizeof(f->value), value);
    f->width = width != 0U ? width : 20U;
    f->secret = secret;
    ++ui->fields;
    ui->caret = rsd_strlen(ui->field[ui->focus].value);
}

void rsd_ui_buttons(struct rsd_ui *ui, const char *a, const char *b,
                     const char *c)
{
    const char *all[RSD_UI_BUTTONS];

    if (ui == NULL) {
        return;
    }
    all[0] = a;
    all[1] = b;
    all[2] = c;
    ui->buttons = 0U;
    for (uint32_t i = 0U; i < RSD_UI_BUTTONS; ++i) {
        if (all[i] == NULL || all[i][0] == '\0') {
            continue;
        }
        (void)rsd_strcopy(ui->button[ui->buttons],
                           sizeof(ui->button[0]), all[i]);
        ++ui->buttons;
    }
    if (ui->chosen >= ui->buttons) {
        ui->chosen = 0U;
    }
}

void rsd_ui_select(struct rsd_ui *ui, uint32_t index)
{
    if (ui == NULL || index >= ui->items) {
        return;
    }
    ui->cursor = index;
    if (ui->visible != 0U && ui->cursor >= ui->top + ui->visible) {
        ui->top = ui->cursor - ui->visible + 1U;
    }
    if (ui->cursor < ui->top) {
        ui->top = ui->cursor;
    }
}

void rsd_ui_help(struct rsd_ui *ui, const char *text)
{
    if (ui == NULL || ui->items == 0U) {
        return;
    }
    (void)rsd_strcopy(ui->item[ui->items - 1U].help,
                       sizeof(ui->item[0].help), text);
}

const char *rsd_ui_hint(const struct rsd_ui *ui)
{
    if (ui == NULL || ui->on_buttons || ui->cursor >= ui->items) {
        return NULL;
    }
    return ui->item[ui->cursor].help[0] != '\0'
           ? ui->item[ui->cursor].help : NULL;
}

void rsd_ui_lock(struct rsd_ui *ui, uint32_t index)
{
    if (ui == NULL || index >= ui->items) {
        return;
    }
    ui->item[index].locked = true;
    ui->item[index].on = true;
}

void rsd_ui_gauge(struct rsd_ui *ui, uint32_t percent, const char *note)
{
    if (ui == NULL) {
        return;
    }
    ui->percent = umin(percent, 100U);
    (void)rsd_strcopy(ui->note, sizeof(ui->note), note);
}

/* ------------------------------------------------------------- reading */

const char *rsd_ui_tag(const struct rsd_ui *ui)
{
    if (ui == NULL || ui->cursor >= ui->items) {
        return "";
    }
    return ui->item[ui->cursor].tag;
}

const char *rsd_ui_value(const struct rsd_ui *ui, uint32_t field)
{
    if (ui == NULL || field >= ui->fields) {
        return "";
    }
    return ui->field[field].value;
}

const char *rsd_ui_button(const struct rsd_ui *ui)
{
    if (ui == NULL || ui->chosen >= ui->buttons) {
        return "";
    }
    return ui->button[ui->chosen];
}

bool rsd_ui_checked(const struct rsd_ui *ui, const char *tag)
{
    if (ui == NULL) {
        return false;
    }
    for (uint32_t i = 0U; i < ui->items; ++i) {
        if (rsd_streq(ui->item[i].tag, tag)) {
            return ui->item[i].on;
        }
    }
    return false;
}

uint32_t rsd_ui_checked_count(const struct rsd_ui *ui)
{
    uint32_t n = 0U;

    for (uint32_t i = 0U; ui != NULL && i < ui->items; ++i) {
        if (ui->item[i].on) {
            ++n;
        }
    }
    return n;
}

/* ------------------------------------------------------------- driving */

static void move_to(struct rsd_ui *ui, uint32_t index)
{
    rsd_ui_select(ui, index);
}

static void step(struct rsd_ui *ui, int delta)
{
    if (ui->items == 0U) {
        return;
    }
    if (delta < 0) {
        uint32_t back = (uint32_t)(-delta);

        move_to(ui, ui->cursor > back ? ui->cursor - back : 0U);
    } else {
        uint32_t fwd = ui->cursor + (uint32_t)delta;

        move_to(ui, fwd < ui->items ? fwd : ui->items - 1U);
    }
}

static void toggle(struct rsd_ui *ui)
{
    if (ui->cursor >= ui->items || ui->item[ui->cursor].locked) {
        return;
    }
    if (ui->kind == RSD_UI_CHECK) {
        ui->item[ui->cursor].on = !ui->item[ui->cursor].on;
    } else if (ui->kind == RSD_UI_RADIO) {
        for (uint32_t i = 0U; i < ui->items; ++i) {
            ui->item[i].on = (i == ui->cursor);
        }
    }
}

/* A letter jumps to the item that starts with it, which is what the
 * red letter in the list is promising. */
static bool jump(struct rsd_ui *ui, char c)
{
    for (uint32_t i = 0U; i < ui->items; ++i) {
        uint32_t at = (ui->cursor + 1U + i) % ui->items;

        if (lower(ui->item[at].tag[0]) == lower(c)) {
            move_to(ui, at);
            return true;
        }
    }
    return false;
}

static void field_focus(struct rsd_ui *ui, uint32_t index)
{
    if (index >= ui->fields) {
        return;
    }
    ui->focus = index;
    ui->caret = rsd_strlen(ui->field[index].value);
}

static void field_insert(struct rsd_ui *ui, char c)
{
    struct rsd_ui_field *f;
    uint32_t len;

    if (ui->focus >= ui->fields) {
        return;
    }
    f = &ui->field[ui->focus];
    len = rsd_strlen(f->value);
    if (len + 1U >= sizeof(f->value) || len >= f->width) {
        return;             /* the box is as long as the box looks */
    }
    for (uint32_t i = len; i > ui->caret; --i) {
        f->value[i] = f->value[i - 1U];
    }
    f->value[ui->caret] = c;
    f->value[len + 1U] = '\0';
    ++ui->caret;
}

static void field_erase(struct rsd_ui *ui)
{
    struct rsd_ui_field *f;
    uint32_t len;

    if (ui->focus >= ui->fields || ui->caret == 0U) {
        return;
    }
    f = &ui->field[ui->focus];
    len = rsd_strlen(f->value);
    for (uint32_t i = ui->caret - 1U; i + 1U < len; ++i) {
        f->value[i] = f->value[i + 1U];
    }
    f->value[len - 1U] = '\0';
    --ui->caret;
}

enum rsd_ui_result rsd_ui_key(struct rsd_ui *ui, int key)
{
    bool has_list;

    if (ui == NULL) {
        return RSD_UI_EDITING;
    }
    has_list = ui->items != 0U || ui->fields != 0U;

    /* Escape leaves, from anywhere, always. An installer you cannot
     * back out of is an installer that has taken the machine hostage. */
    if (key == 0x1B) {
        return RSD_UI_REJECT;
    }
    /* F1 is not a key this widget can answer. It hands the question up
     * to whoever built the screen, because the help is about what the
     * screen is asking, not about how a list works. */
    if (key == RSD_KEY_F1) {
        return RSD_UI_ASKED;
    }
    if (key == '\r' || key == '\n') {
        return RSD_UI_ACCEPT;
    }
    if (key == '\t') {
        if (!has_list) {
            ui->chosen = ui->buttons != 0U
                         ? (ui->chosen + 1U) % ui->buttons : 0U;
            return RSD_UI_EDITING;
        }
        if (!ui->on_buttons) {
            ui->on_buttons = true;
            ui->chosen = 0U;
        } else if (ui->chosen + 1U < ui->buttons) {
            ++ui->chosen;
        } else {
            ui->on_buttons = false;
            ui->chosen = 0U;
        }
        return RSD_UI_EDITING;
    }
    if (key == RSD_KEY_LEFT || key == RSD_KEY_RIGHT) {
        /* On the button row, and in a field, left and right mean two
         * different obvious things, and neither is the other's. */
        if (ui->on_buttons || !has_list) {
            if (ui->buttons != 0U) {
                if (key == RSD_KEY_LEFT && ui->chosen != 0U) {
                    --ui->chosen;
                } else if (key == RSD_KEY_RIGHT
                           && ui->chosen + 1U < ui->buttons) {
                    ++ui->chosen;
                }
            }
        } else if (ui->kind == RSD_UI_FORM) {
            if (key == RSD_KEY_LEFT && ui->caret != 0U) {
                --ui->caret;
            } else if (key == RSD_KEY_RIGHT
                       && ui->caret < rsd_strlen(
                              ui->field[ui->focus].value)) {
                ++ui->caret;
            }
        }
        return RSD_UI_EDITING;
    }
    if (key == RSD_KEY_UP || key == RSD_KEY_DOWN) {
        int delta = key == RSD_KEY_UP ? -1 : 1;

        if (ui->on_buttons) {
            if (delta < 0) {
                ui->on_buttons = false;     /* back up into the list */
            }
            return RSD_UI_EDITING;
        }
        if (ui->kind == RSD_UI_FORM) {
            if (delta < 0 && ui->focus != 0U) {
                field_focus(ui, ui->focus - 1U);
            } else if (delta > 0 && ui->focus + 1U < ui->fields) {
                field_focus(ui, ui->focus + 1U);
            } else if (delta > 0) {
                ui->on_buttons = true;      /* off the end, onto OK */
            }
            return RSD_UI_EDITING;
        }
        step(ui, delta);
        return RSD_UI_EDITING;
    }
    if (key == RSD_KEY_PGUP || key == RSD_KEY_PGDN) {
        step(ui, key == RSD_KEY_PGUP ? -(int)ui->visible
                                      : (int)ui->visible);
        return RSD_UI_EDITING;
    }
    if (key == RSD_KEY_HOME && ui->kind != RSD_UI_FORM) {
        move_to(ui, 0U);
        return RSD_UI_EDITING;
    }
    if (key == RSD_KEY_END && ui->kind != RSD_UI_FORM) {
        if (ui->items != 0U) {
            move_to(ui, ui->items - 1U);
        }
        return RSD_UI_EDITING;
    }
    if (ui->kind == RSD_UI_FORM && !ui->on_buttons) {
        if (key == RSD_KEY_HOME) {
            ui->caret = 0U;
            return RSD_UI_EDITING;
        }
        if (key == RSD_KEY_END) {
            ui->caret = rsd_strlen(ui->field[ui->focus].value);
            return RSD_UI_EDITING;
        }
        if (key == 0x08 || key == 0x7F) {
            field_erase(ui);
            return RSD_UI_EDITING;
        }
        if (key >= 0x20 && key <= 0x7E) {
            field_insert(ui, (char)key);
            return RSD_UI_EDITING;
        }
        return RSD_UI_EDITING;
    }
    if (key == ' ') {
        if (ui->on_buttons) {
            return RSD_UI_ACCEPT;
        }
        toggle(ui);
        return RSD_UI_EDITING;
    }
    if (key >= 0x21 && key <= 0x7E) {
        if (!ui->on_buttons && jump(ui, (char)key)) {
            return RSD_UI_EDITING;
        }
        /* Not an item: try the buttons, where the red letter is the
         * whole point of the red letter. */
        for (uint32_t i = 0U; i < ui->buttons; ++i) {
            if (lower(ui->button[i][0]) == lower((char)key)) {
                ui->chosen = i;
                ui->on_buttons = true;
                return RSD_UI_ACCEPT;
            }
        }
    }
    return RSD_UI_EDITING;
}

/* ------------------------------------------------------------- drawing */

void rsd_ui_backdrop(struct rsd_term *t, const char *backtitle,
                      const char *hint)
{
    if (t == NULL) {
        return;
    }
    rsd_term_fill(t, ' ', RSD_C_FIELD);
    rsd_term_puts_at(t, 0U, 1U, backtitle, RSD_C_BACKTITLE);
    rsd_term_repeat_at(t, 1U, 0U, RSD_G_HLINE, RSD_COLS, RSD_C_FIELD);
    /*
     * The line about the highlighted item goes along the very bottom,
     * left-aligned, white on black - dialog's ITEMHELP, which is the
     * one thing on a bsdinstall screen that is not on the blue field.
     * It was centred in yellow, which is neither where dialog puts it
     * nor what dialog puts there.
     */
    if (hint != NULL && hint[0] != '\0') {
        rsd_term_repeat_at(t, RSD_ROWS - 1U, 0U, ' ', RSD_COLS,
                            RSD_C_ITEMHELP);
        rsd_term_puts_at(t, RSD_ROWS - 1U, 1U, hint, RSD_C_ITEMHELP);
    }
    t->cursor = false;
}

/*
 * The box, with or without the shadow under it.
 *
 * dialog draws the same box twice on a gauge screen - the dialog itself
 * and a smaller one round the bar - with dlg_draw_box2() both times, and
 * only the outer one gets a shadow. A shadow on the inner box is a
 * shadow cast by something that is not above anything.
 */
static void box(struct rsd_term *t, uint32_t row, uint32_t col,
                uint32_t rows, uint32_t cols, const char *title,
                bool shadow)
{
    uint32_t last_r;
    uint32_t last_c;

    if (t == NULL || rows < 2U || cols < 2U) {
        return;
    }
    last_r = row + rows - 1U;
    last_c = col + cols - 1U;

    /*
     * The shadow first, because the box is drawn over the top of it.
     * One row down and two columns right - SHADOW_ROWS 1 and
     * SHADOW_COLS 2, out of dialog.h.
     */
    if (shadow) {
        rsd_term_box_fill(t, row + 1U, col + cols, rows, 2U, ' ',
                           RSD_C_SHADOW);
        rsd_term_repeat_at(t, last_r + 1U, col + 2U, ' ', cols,
                            RSD_C_SHADOW);
    }

    rsd_term_box_fill(t, row, col, rows, cols, ' ', RSD_C_BOX);

    /* Lit from the top left: the top and the left edge catch it, the
     * bottom and the right do not. */
    rsd_term_put_at(t, row, col, RSD_G_UL, RSD_C_EDGE_HI);
    rsd_term_repeat_at(t, row, col + 1U, RSD_G_HLINE, cols - 2U,
                        RSD_C_EDGE_HI);
    rsd_term_put_at(t, row, last_c, RSD_G_UR, RSD_C_EDGE_HI);
    for (uint32_t r = row + 1U; r < last_r; ++r) {
        rsd_term_put_at(t, r, col, RSD_G_VLINE, RSD_C_EDGE_HI);
        rsd_term_put_at(t, r, last_c, RSD_G_VLINE, RSD_C_EDGE_LO);
    }
    rsd_term_put_at(t, last_r, col, RSD_G_LL, RSD_C_EDGE_HI);
    rsd_term_repeat_at(t, last_r, col + 1U, RSD_G_HLINE, cols - 2U,
                        RSD_C_EDGE_LO);
    rsd_term_put_at(t, last_r, last_c, RSD_G_LR, RSD_C_EDGE_LO);

    if (title != NULL && title[0] != '\0') {
        uint32_t len = rsd_strlen(title);
        uint32_t at = col + rsd_ui_centre(len + 2U, cols);

        rsd_term_put_at(t, row, at, ' ', RSD_C_TITLE);
        rsd_term_puts_at(t, row, at + 1U, title, RSD_C_TITLE);
        rsd_term_put_at(t, row, at + 1U + len, ' ', RSD_C_TITLE);
    }
}

void rsd_ui_frame(struct rsd_term *t, uint32_t row, uint32_t col,
                   uint32_t rows, uint32_t cols, const char *title)
{
    box(t, row, col, rows, cols, title, true);
}

/* One item: its marker, its tag with the first letter lit, its note. */
static void draw_item(const struct rsd_ui *ui, struct rsd_term *t,
                      uint32_t index, uint32_t row, uint32_t col,
                      uint32_t width)
{
    const struct rsd_ui_item *it = &ui->item[index];
    /* The highlight stays on the chosen item while the focus is down
     * on the buttons: pressing OK is about to act on it, so hiding
     * which one it is would be hiding the only thing that matters. */
    bool sel = index == ui->cursor;
    uint8_t body = sel ? RSD_C_ITEM_SEL : RSD_C_ITEM;
    uint8_t key = sel ? RSD_C_KEY_SEL : RSD_C_KEY;
    uint32_t at = col;

    rsd_term_repeat_at(t, row, col, ' ', width, body);
    if (ui->kind == RSD_UI_CHECK) {
        rsd_term_put_at(t, row, at, '[', body);
        rsd_term_put_at(t, row, at + 1U, it->on ? 'X' : ' ', body);
        rsd_term_put_at(t, row, at + 2U, ']', body);
        at += 4U;
    } else if (ui->kind == RSD_UI_RADIO) {
        rsd_term_put_at(t, row, at, '(', body);
        rsd_term_put_at(t, row, at + 1U, it->on ? '*' : ' ', body);
        rsd_term_put_at(t, row, at + 2U, ')', body);
        at += 4U;
    }
    rsd_term_puts_at(t, row, at, it->tag, body);
    /* The lit letter is the one you can press. A tag that starts with a
     * space is indented under the one above it and has no such letter,
     * so nothing is lit and nothing is promised. */
    if (it->tag[0] > ' ') {
        rsd_term_put_at(t, row, at, it->tag[0], key);
    }
    if (it->desc[0] != '\0') {
        rsd_term_puts_at(t, row, at + tag_width(ui) + 2U, it->desc, body);
    }
}

static void draw_buttons(const struct rsd_ui *ui, struct rsd_term *t,
                         uint32_t row, uint32_t col, uint32_t cols)
{
    uint32_t bw = button_width(ui);
    uint32_t at = col + rsd_ui_centre(button_row_width(ui), cols);

    for (uint32_t i = 0U; i < ui->buttons; ++i) {
        const char *label = ui->button[i];
        uint32_t len = rsd_strlen(label);
        bool sel = ui->on_buttons && i == ui->chosen;
        uint8_t body = sel ? RSD_C_ITEM_SEL : RSD_C_ITEM;
        uint8_t key = sel ? RSD_C_KEY_SEL : RSD_C_KEY;
        uint32_t text_at = at + 1U + rsd_ui_centre(len, bw - 2U);

        rsd_term_put_at(t, row, at, '<', RSD_C_BOX);
        rsd_term_repeat_at(t, row, at + 1U, ' ', bw - 2U, body);
        rsd_term_puts_at(t, row, text_at, label, body);
        if (len != 0U) {
            rsd_term_put_at(t, row, text_at, label[0], key);
        }
        rsd_term_put_at(t, row, at + bw - 1U, '>', RSD_C_BOX);
        at += bw + 2U;
    }
}

static void draw_field(const struct rsd_ui *ui, struct rsd_term *t,
                       uint32_t index, uint32_t row, uint32_t col)
{
    const struct rsd_ui_field *f = &ui->field[index];
    uint32_t lw = label_width(ui);
    uint32_t box = col + lw + 1U;
    uint32_t len = rsd_strlen(f->value);

    rsd_term_puts_at(t, row, col, f->label, RSD_C_ITEM);
    rsd_term_repeat_at(t, row, box, ' ', f->width, RSD_C_INPUT);
    if (f->secret) {
        /*
         * NOTHING AT ALL, not even a count.
         *
         * This drew an asterisk per key, which is a choice and a
         * common one - and it is not dialog's. A --passwordbox shows
         * nothing unless it is passed --insecure, for the reason the
         * option's name gives: the length of a password is worth
         * something to somebody standing behind you. It is also what
         * passwd(1) does, which is what bsdinstall actually runs.
         *
         * A field that shows nothing has to SAY it shows nothing, or
         * the first thing anybody does is type it twice. The screen
         * does, in the same breath as it asks.
         */
        (void)len;
    } else {
        rsd_term_puts_at(t, row, box, f->value, RSD_C_INPUT);
    }
    /* The caret is a block, on the field being typed into, and only on
     * that one - two carets would be two places to type. */
    if (index == ui->focus && !ui->on_buttons) {
        uint32_t at = box + umin(ui->caret, f->width - 1U);

        rsd_term_put_at(t, row, at, rsd_term_at(t, row, at),
                         RSD_ATTR(RSD_BLUE, RSD_WHITE));
    }
}

/*
 * THE GAUGE, as dialog draws one - which is not a hatched trough with a
 * block crawling along it.
 *
 * From guage.c repaint_text(): a BOX is drawn round the bar, the whole
 * trough is filled with SPACES in gauge_attr (blue on white, so it
 * reads as plain grey), the percentage is printed centred in that same
 * attribute, and then the first x cells are redrawn REVERSED - which
 * turns blue-on-grey into grey-on-blue, so the bar is a solid blue block
 * and the digits standing on it swap to grey without moving.
 *
 * That last part is why the number sits on the bar rather than under
 * it: it is one string, drawn once, and the bar passing through it
 * changes its colour a character at a time.
 */
static void draw_gauge(const struct rsd_ui *ui, struct rsd_term *t,
                       uint32_t row, uint32_t col, uint32_t width)
{
    /* Inside the box round it, which takes a column either side. */
    uint32_t left = col + 1U;
    uint32_t span = width > 2U ? width - 2U : width;
    uint32_t done = (span * ui->percent) / 100U;
    uint32_t bar = row + 1U;
    char pc[8];
    uint32_t len;
    uint32_t at;

    box(t, row, col, 3U, width, NULL, false);
    rsd_term_repeat_at(t, bar, left, ' ', span, RSD_C_GAUGE);
    rsd_term_repeat_at(t, bar, left, ' ', done, RSD_C_GAUGE_ON);

    (void)rsd_u32(pc, sizeof(pc), ui->percent);
    (void)rsd_strcat(pc, sizeof(pc), "%");
    len = rsd_strlen(pc);
    at = left + rsd_ui_centre(len, span);
    for (uint32_t i = 0U; i < len; ++i) {
        bool filled = at + i < left + done;

        rsd_term_put_at(t, bar, at + i, pc[i],
                         filled ? RSD_C_GAUGE_ON : RSD_C_GAUGE);
    }
}

void rsd_ui_draw(const struct rsd_ui *ui, struct rsd_term *t)
{
    uint32_t inner;
    uint32_t cols;
    uint32_t rows;
    uint32_t text_lines;
    uint32_t body_rows;
    uint32_t row;
    uint32_t col;
    uint32_t at;
    char line[RSD_COLS + 1];

    if (ui == NULL || t == NULL) {
        return;
    }

    /* Width comes from the widest thing in the box. */
    cols = BOX_MIN;
    cols = umax(cols, rsd_strlen(ui->title) + 8U);
    cols = umax(cols, item_width(ui) + 2U * PAD + 2U);
    cols = umax(cols, form_width(ui) + 2U * PAD + 2U);
    cols = umax(cols, button_row_width(ui) + 2U * PAD + 2U);
    /* The line under a gauge is as much a part of it as the bar. */
    cols = umax(cols, rsd_strlen(ui->note) + 2U * PAD + 2U);
    if (ui->text[0] != '\0') {
        cols = umax(cols, 60U);
    }
    cols = umin(cols, BOX_MAX);
    inner = cols - 2U * PAD;

    text_lines = wrap(ui->text, inner, (uint32_t)-1, NULL, 0U);
    if (ui->kind == RSD_UI_FORM) {
        body_rows = ui->fields;
    } else if (ui->kind == RSD_UI_GAUGE) {
        /* The boxed bar is three rows, then a blank and the line
         * saying what it is doing - and nothing at all when there is
         * no such line. */
        body_rows = 3U + (ui->note[0] != '\0' ? 2U : 0U);
    } else {
        body_rows = ui->visible;
    }

    /* 2 borders, a blank line under the text, a blank line over the
     * buttons, and the button row itself. */
    rows = 2U + (text_lines != 0U ? text_lines + 1U : 0U) + body_rows;
    rows += ui->buttons != 0U ? 2U : 0U;
    if (rows > RSD_ROWS - 4U) {
        rows = RSD_ROWS - 4U;
    }

    col = rsd_ui_centre(cols, RSD_COLS);
    row = 2U + rsd_ui_centre(rows, RSD_ROWS - 4U);

    rsd_ui_frame(t, row, col, rows, cols, ui->title);

    at = row + 1U;
    for (uint32_t i = 0U; i < text_lines; ++i) {
        (void)wrap(ui->text, inner, i, line, sizeof(line));
        rsd_term_puts_at(t, at, col + PAD, line, RSD_C_ITEM);
        ++at;
    }
    if (text_lines != 0U) {
        ++at;
    }

    if (ui->kind == RSD_UI_FORM) {
        for (uint32_t i = 0U; i < ui->fields; ++i) {
            draw_field(ui, t, i, at + i, col + PAD);
        }
        at += ui->fields;
    } else if (ui->kind == RSD_UI_GAUGE) {
        draw_gauge(ui, t, at, col + PAD, inner);
        at += 3U;
        if (ui->note[0] != '\0') {
            ++at;
            rsd_term_puts_at(t, at, col + PAD, ui->note, RSD_C_ITEM);
            ++at;
        }
    } else {
        uint32_t shown = umin(ui->visible, ui->items);

        for (uint32_t i = 0U; i < shown; ++i) {
            draw_item(ui, t, ui->top + i, at + i, col + PAD, inner);
        }
        /* A list longer than its window says which way the rest is. */
        if (ui->top != 0U) {
            rsd_term_put_at(t, at, col + cols - 2U, RSD_G_UP,
                             RSD_C_EDGE_LO);
        }
        if (ui->top + shown < ui->items) {
            rsd_term_put_at(t, at + shown - 1U, col + cols - 2U,
                             RSD_G_DOWN, RSD_C_EDGE_LO);
        }
        at += shown;
    }

    if (ui->buttons != 0U) {
        draw_buttons(ui, t, row + rows - 2U, col, cols);
    }
    /*
     * The key legend, sunk into the BOTTOM border - dialog's --hline,
     * which is drawn over the border's line the way the title is drawn
     * over the top one, with a space either side of it so the rule does
     * not run into the words.
     */
    if (ui->hline[0] != '\0') {
        uint32_t len = rsd_strlen(ui->hline);
        uint32_t into = col + rsd_ui_centre(len + 2U, cols);

        if (len + 4U < cols) {
            rsd_term_put_at(t, row + rows - 1U, into, ' ',
                             RSD_C_EDGE_LO);
            rsd_term_puts_at(t, row + rows - 1U, into + 1U, ui->hline,
                              RSD_C_EDGE_LO);
            rsd_term_put_at(t, row + rows - 1U, into + 1U + len, ' ',
                             RSD_C_EDGE_LO);
        }
    }
    t->cursor = false;
}

void rsd_ui_hline(struct rsd_ui *ui, const char *text)
{
    if (ui == NULL) {
        return;
    }
    (void)rsd_strcopy(ui->hline, sizeof(ui->hline),
                       text != NULL ? text : "");
}

/*
 * WHAT RETURN WOULD TAKE.
 *
 * One function, used both to print the bracket and - by the installer -
 * to write the answer into the transcript once it has been taken. Two
 * functions would be two chances for the screen to say [yes] and the
 * machine to hear no.
 */
void rsd_ui_default(const struct rsd_ui *ui, char *out,
                     uint32_t capacity)
{
    if (out == NULL || capacity == 0U) {
        return;
    }
    out[0] = '\0';
    if (ui == NULL) {
        return;
    }
    switch (ui->kind) {
    case RSD_UI_FORM:
        if (ui->fields != 0U) {
            const struct rsd_ui_field *f = &ui->field[ui->focus];

            if (f->secret) {
                /* A password is not echoed and not defaulted; OpenBSD
                 * asks for it twice instead. */
                return;
            }
            (void)rsd_strcopy(out, capacity, f->value);
        }
        return;
    case RSD_UI_CHECK:
        for (uint32_t i = 0U; i < ui->items; ++i) {
            if (!ui->item[i].on) {
                continue;
            }
            if (out[0] != '\0') {
                (void)rsd_strcat(out, capacity, " ");
            }
            (void)rsd_strcat(out, capacity, ui->item[i].tag);
        }
        return;
    case RSD_UI_MSG:
        if (ui->buttons != 0U) {
            (void)rsd_strcopy(out, capacity, ui->button[ui->chosen]);
        }
        return;
    case RSD_UI_GAUGE:
        return;
    default:
        if (ui->items != 0U) {
            (void)rsd_strcopy(out, capacity, ui->item[ui->cursor].tag);
        }
        return;
    }
}
