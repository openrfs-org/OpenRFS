/* SPDX-License-Identifier: GPL-3.0-only */
#include <orfs/ui.h>

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

uint32_t orfs_ui_centre(uint32_t width, uint32_t cols)
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
static uint32_t button_width(const struct orfs_ui *ui)
{
    uint32_t w = 0U;

    for (uint32_t i = 0U; i < ui->buttons; ++i) {
        w = umax(w, orfs_strlen(ui->button[i]));
    }
    /* Every button is as wide as the widest label, so the row is even,
     * and the brackets sit tight against the longest one: <Cancel> and
     * <  OK  > and <Cancel>, the way dialog(1) draws them. */
    return w + 2U;
}

static uint32_t button_row_width(const struct orfs_ui *ui)
{
    if (ui->buttons == 0U) {
        return 0U;
    }
    return ui->buttons * button_width(ui) + (ui->buttons - 1U) * 2U;
}

/* How wide an item is drawn, marker included. */
static uint32_t marker_width(const struct orfs_ui *ui)
{
    return (ui->kind == ORFS_UI_CHECK || ui->kind == ORFS_UI_RADIO)
           ? 4U : 0U;
}

static uint32_t tag_width(const struct orfs_ui *ui)
{
    uint32_t w = 0U;

    for (uint32_t i = 0U; i < ui->items; ++i) {
        w = umax(w, orfs_strlen(ui->item[i].tag));
    }
    return w;
}

static uint32_t item_width(const struct orfs_ui *ui)
{
    uint32_t desc = 0U;

    for (uint32_t i = 0U; i < ui->items; ++i) {
        desc = umax(desc, orfs_strlen(ui->item[i].desc));
    }
    return marker_width(ui) + tag_width(ui) + (desc != 0U ? 2U + desc : 0U);
}

static uint32_t label_width(const struct orfs_ui *ui)
{
    uint32_t w = 0U;

    for (uint32_t i = 0U; i < ui->fields; ++i) {
        w = umax(w, orfs_strlen(ui->field[i].label));
    }
    return w;
}

static uint32_t form_width(const struct orfs_ui *ui)
{
    uint32_t v = 0U;

    for (uint32_t i = 0U; i < ui->fields; ++i) {
        v = umax(v, ui->field[i].width);
    }
    return label_width(ui) + 1U + v;
}

/* ------------------------------------------------------------ building */

void orfs_ui_begin(struct orfs_ui *ui, enum orfs_ui_kind kind,
                   const char *title, const char *text)
{
    if (ui == NULL) {
        return;
    }
    for (uint32_t i = 0U; i < ORFS_UI_ITEMS; ++i) {
        ui->item[i].tag[0] = '\0';
        ui->item[i].desc[0] = '\0';
        ui->item[i].help[0] = '\0';
        ui->item[i].on = false;
        ui->item[i].locked = false;
    }
    for (uint32_t i = 0U; i < ORFS_UI_FIELDS; ++i) {
        ui->field[i].label[0] = '\0';
        ui->field[i].value[0] = '\0';
        ui->field[i].width = 0U;
        ui->field[i].secret = false;
    }
    for (uint32_t i = 0U; i < ORFS_UI_BUTTONS; ++i) {
        ui->button[i][0] = '\0';
    }
    ui->kind = kind;
    (void)orfs_strcopy(ui->title, sizeof(ui->title), title);
    (void)orfs_strcopy(ui->text, sizeof(ui->text), text);
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
    ui->on_buttons = kind == ORFS_UI_MSG;
    ui->percent = 0U;
    ui->note[0] = '\0';
    /* Every dialog has somewhere to go and a way out of it. A screen
     * built without calling orfs_ui_buttons still has these. */
    orfs_ui_buttons(ui, "OK", "Cancel", NULL);
}

void orfs_ui_item(struct orfs_ui *ui, const char *tag, const char *desc,
                  bool on)
{
    struct orfs_ui_item *it;

    if (ui == NULL || ui->items >= ORFS_UI_ITEMS) {
        return;
    }
    it = &ui->item[ui->items];
    (void)orfs_strcopy(it->tag, sizeof(it->tag), tag);
    (void)orfs_strcopy(it->desc, sizeof(it->desc), desc);
    it->help[0] = '\0';
    it->on = on;
    it->locked = false;
    ++ui->items;
    /* A list shows eight rows, or all of them if there are fewer. Past
     * eight it scrolls, and says so with an arrow. */
    ui->visible = umin(ui->items, 8U);
}

void orfs_ui_field(struct orfs_ui *ui, const char *label, const char *value,
                   uint32_t width, bool secret)
{
    struct orfs_ui_field *f;

    if (ui == NULL || ui->fields >= ORFS_UI_FIELDS) {
        return;
    }
    f = &ui->field[ui->fields];
    (void)orfs_strcopy(f->label, sizeof(f->label), label);
    (void)orfs_strcopy(f->value, sizeof(f->value), value);
    f->width = width != 0U ? width : 20U;
    f->secret = secret;
    ++ui->fields;
    ui->caret = orfs_strlen(ui->field[ui->focus].value);
}

void orfs_ui_buttons(struct orfs_ui *ui, const char *a, const char *b,
                     const char *c)
{
    const char *all[ORFS_UI_BUTTONS];

    if (ui == NULL) {
        return;
    }
    all[0] = a;
    all[1] = b;
    all[2] = c;
    ui->buttons = 0U;
    for (uint32_t i = 0U; i < ORFS_UI_BUTTONS; ++i) {
        if (all[i] == NULL || all[i][0] == '\0') {
            continue;
        }
        (void)orfs_strcopy(ui->button[ui->buttons],
                           sizeof(ui->button[0]), all[i]);
        ++ui->buttons;
    }
    if (ui->chosen >= ui->buttons) {
        ui->chosen = 0U;
    }
}

void orfs_ui_select(struct orfs_ui *ui, uint32_t index)
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

void orfs_ui_help(struct orfs_ui *ui, const char *text)
{
    if (ui == NULL || ui->items == 0U) {
        return;
    }
    (void)orfs_strcopy(ui->item[ui->items - 1U].help,
                       sizeof(ui->item[0].help), text);
}

const char *orfs_ui_hint(const struct orfs_ui *ui)
{
    if (ui == NULL || ui->on_buttons || ui->cursor >= ui->items) {
        return NULL;
    }
    return ui->item[ui->cursor].help[0] != '\0'
           ? ui->item[ui->cursor].help : NULL;
}

void orfs_ui_lock(struct orfs_ui *ui, uint32_t index)
{
    if (ui == NULL || index >= ui->items) {
        return;
    }
    ui->item[index].locked = true;
    ui->item[index].on = true;
}

void orfs_ui_gauge(struct orfs_ui *ui, uint32_t percent, const char *note)
{
    if (ui == NULL) {
        return;
    }
    ui->percent = umin(percent, 100U);
    (void)orfs_strcopy(ui->note, sizeof(ui->note), note);
}

/* ------------------------------------------------------------- reading */

const char *orfs_ui_tag(const struct orfs_ui *ui)
{
    if (ui == NULL || ui->cursor >= ui->items) {
        return "";
    }
    return ui->item[ui->cursor].tag;
}

const char *orfs_ui_value(const struct orfs_ui *ui, uint32_t field)
{
    if (ui == NULL || field >= ui->fields) {
        return "";
    }
    return ui->field[field].value;
}

const char *orfs_ui_button(const struct orfs_ui *ui)
{
    if (ui == NULL || ui->chosen >= ui->buttons) {
        return "";
    }
    return ui->button[ui->chosen];
}

bool orfs_ui_checked(const struct orfs_ui *ui, const char *tag)
{
    if (ui == NULL) {
        return false;
    }
    for (uint32_t i = 0U; i < ui->items; ++i) {
        if (orfs_streq(ui->item[i].tag, tag)) {
            return ui->item[i].on;
        }
    }
    return false;
}

uint32_t orfs_ui_checked_count(const struct orfs_ui *ui)
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

static void move_to(struct orfs_ui *ui, uint32_t index)
{
    orfs_ui_select(ui, index);
}

static void step(struct orfs_ui *ui, int delta)
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

static void toggle(struct orfs_ui *ui)
{
    if (ui->cursor >= ui->items || ui->item[ui->cursor].locked) {
        return;
    }
    if (ui->kind == ORFS_UI_CHECK) {
        ui->item[ui->cursor].on = !ui->item[ui->cursor].on;
    } else if (ui->kind == ORFS_UI_RADIO) {
        for (uint32_t i = 0U; i < ui->items; ++i) {
            ui->item[i].on = (i == ui->cursor);
        }
    }
}

/* A letter jumps to the item that starts with it, which is what the
 * red letter in the list is promising. */
static bool jump(struct orfs_ui *ui, char c)
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

static void field_focus(struct orfs_ui *ui, uint32_t index)
{
    if (index >= ui->fields) {
        return;
    }
    ui->focus = index;
    ui->caret = orfs_strlen(ui->field[index].value);
}

static void field_insert(struct orfs_ui *ui, char c)
{
    struct orfs_ui_field *f;
    uint32_t len;

    if (ui->focus >= ui->fields) {
        return;
    }
    f = &ui->field[ui->focus];
    len = orfs_strlen(f->value);
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

static void field_erase(struct orfs_ui *ui)
{
    struct orfs_ui_field *f;
    uint32_t len;

    if (ui->focus >= ui->fields || ui->caret == 0U) {
        return;
    }
    f = &ui->field[ui->focus];
    len = orfs_strlen(f->value);
    for (uint32_t i = ui->caret - 1U; i + 1U < len; ++i) {
        f->value[i] = f->value[i + 1U];
    }
    f->value[len - 1U] = '\0';
    --ui->caret;
}

enum orfs_ui_result orfs_ui_key(struct orfs_ui *ui, int key)
{
    bool has_list;

    if (ui == NULL) {
        return ORFS_UI_EDITING;
    }
    has_list = ui->items != 0U || ui->fields != 0U;

    /* Escape leaves, from anywhere, always. An installer you cannot
     * back out of is an installer that has taken the machine hostage. */
    if (key == 0x1B) {
        return ORFS_UI_REJECT;
    }
    /* F1 is not a key this widget can answer. It hands the question up
     * to whoever built the screen, because the help is about what the
     * screen is asking, not about how a list works. */
    if (key == ORFS_KEY_F1) {
        return ORFS_UI_ASKED;
    }
    if (key == '\r' || key == '\n') {
        return ORFS_UI_ACCEPT;
    }
    if (key == '\t') {
        if (!has_list) {
            ui->chosen = ui->buttons != 0U
                         ? (ui->chosen + 1U) % ui->buttons : 0U;
            return ORFS_UI_EDITING;
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
        return ORFS_UI_EDITING;
    }
    if (key == ORFS_KEY_LEFT || key == ORFS_KEY_RIGHT) {
        /* On the button row, and in a field, left and right mean two
         * different obvious things, and neither is the other's. */
        if (ui->on_buttons || !has_list) {
            if (ui->buttons != 0U) {
                if (key == ORFS_KEY_LEFT && ui->chosen != 0U) {
                    --ui->chosen;
                } else if (key == ORFS_KEY_RIGHT
                           && ui->chosen + 1U < ui->buttons) {
                    ++ui->chosen;
                }
            }
        } else if (ui->kind == ORFS_UI_FORM) {
            if (key == ORFS_KEY_LEFT && ui->caret != 0U) {
                --ui->caret;
            } else if (key == ORFS_KEY_RIGHT
                       && ui->caret < orfs_strlen(
                              ui->field[ui->focus].value)) {
                ++ui->caret;
            }
        }
        return ORFS_UI_EDITING;
    }
    if (key == ORFS_KEY_UP || key == ORFS_KEY_DOWN) {
        int delta = key == ORFS_KEY_UP ? -1 : 1;

        if (ui->on_buttons) {
            if (delta < 0) {
                ui->on_buttons = false;     /* back up into the list */
            }
            return ORFS_UI_EDITING;
        }
        if (ui->kind == ORFS_UI_FORM) {
            if (delta < 0 && ui->focus != 0U) {
                field_focus(ui, ui->focus - 1U);
            } else if (delta > 0 && ui->focus + 1U < ui->fields) {
                field_focus(ui, ui->focus + 1U);
            } else if (delta > 0) {
                ui->on_buttons = true;      /* off the end, onto OK */
            }
            return ORFS_UI_EDITING;
        }
        step(ui, delta);
        return ORFS_UI_EDITING;
    }
    if (key == ORFS_KEY_PGUP || key == ORFS_KEY_PGDN) {
        step(ui, key == ORFS_KEY_PGUP ? -(int)ui->visible
                                      : (int)ui->visible);
        return ORFS_UI_EDITING;
    }
    if (key == ORFS_KEY_HOME && ui->kind != ORFS_UI_FORM) {
        move_to(ui, 0U);
        return ORFS_UI_EDITING;
    }
    if (key == ORFS_KEY_END && ui->kind != ORFS_UI_FORM) {
        if (ui->items != 0U) {
            move_to(ui, ui->items - 1U);
        }
        return ORFS_UI_EDITING;
    }
    if (ui->kind == ORFS_UI_FORM && !ui->on_buttons) {
        if (key == ORFS_KEY_HOME) {
            ui->caret = 0U;
            return ORFS_UI_EDITING;
        }
        if (key == ORFS_KEY_END) {
            ui->caret = orfs_strlen(ui->field[ui->focus].value);
            return ORFS_UI_EDITING;
        }
        if (key == 0x08 || key == 0x7F) {
            field_erase(ui);
            return ORFS_UI_EDITING;
        }
        if (key >= 0x20 && key <= 0x7E) {
            field_insert(ui, (char)key);
            return ORFS_UI_EDITING;
        }
        return ORFS_UI_EDITING;
    }
    if (key == ' ') {
        if (ui->on_buttons) {
            return ORFS_UI_ACCEPT;
        }
        toggle(ui);
        return ORFS_UI_EDITING;
    }
    if (key >= 0x21 && key <= 0x7E) {
        if (!ui->on_buttons && jump(ui, (char)key)) {
            return ORFS_UI_EDITING;
        }
        /* Not an item: try the buttons, where the red letter is the
         * whole point of the red letter. */
        for (uint32_t i = 0U; i < ui->buttons; ++i) {
            if (lower(ui->button[i][0]) == lower((char)key)) {
                ui->chosen = i;
                ui->on_buttons = true;
                return ORFS_UI_ACCEPT;
            }
        }
    }
    return ORFS_UI_EDITING;
}

/* ------------------------------------------------------------- drawing */

void orfs_ui_backdrop(struct orfs_term *t, const char *backtitle,
                      const char *hint)
{
    if (t == NULL) {
        return;
    }
    orfs_term_fill(t, ' ', ORFS_C_FIELD);
    orfs_term_puts_at(t, 0U, 1U, backtitle, ORFS_C_BACKTITLE);
    orfs_term_repeat_at(t, 1U, 0U, ORFS_G_HLINE, ORFS_COLS, ORFS_C_FIELD);
    if (hint != NULL && hint[0] != '\0') {
        orfs_term_puts_at(t, ORFS_ROWS - 1U,
                          orfs_ui_centre(orfs_strlen(hint), ORFS_COLS),
                          hint, ORFS_C_HINT);
    }
    t->cursor = false;
}

void orfs_ui_frame(struct orfs_term *t, uint32_t row, uint32_t col,
                   uint32_t rows, uint32_t cols, const char *title)
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
     * One row down and two columns right, which is where a dialog's
     * dialog(1) puts its shadow.
     */
    orfs_term_box_fill(t, row + 1U, col + cols, rows, 2U, ' ',
                       ORFS_C_SHADOW);
    orfs_term_repeat_at(t, last_r + 1U, col + 2U, ' ', cols, ORFS_C_SHADOW);

    orfs_term_box_fill(t, row, col, rows, cols, ' ', ORFS_C_BOX);

    /* Lit from the top left: the top and the left edge catch it, the
     * bottom and the right do not. */
    orfs_term_put_at(t, row, col, ORFS_G_UL, ORFS_C_EDGE_HI);
    orfs_term_repeat_at(t, row, col + 1U, ORFS_G_HLINE, cols - 2U,
                        ORFS_C_EDGE_HI);
    orfs_term_put_at(t, row, last_c, ORFS_G_UR, ORFS_C_EDGE_HI);
    for (uint32_t r = row + 1U; r < last_r; ++r) {
        orfs_term_put_at(t, r, col, ORFS_G_VLINE, ORFS_C_EDGE_HI);
        orfs_term_put_at(t, r, last_c, ORFS_G_VLINE, ORFS_C_EDGE_LO);
    }
    orfs_term_put_at(t, last_r, col, ORFS_G_LL, ORFS_C_EDGE_HI);
    orfs_term_repeat_at(t, last_r, col + 1U, ORFS_G_HLINE, cols - 2U,
                        ORFS_C_EDGE_LO);
    orfs_term_put_at(t, last_r, last_c, ORFS_G_LR, ORFS_C_EDGE_LO);

    if (title != NULL && title[0] != '\0') {
        uint32_t len = orfs_strlen(title);
        uint32_t at = col + orfs_ui_centre(len + 2U, cols);

        orfs_term_put_at(t, row, at, ' ', ORFS_C_TITLE);
        orfs_term_puts_at(t, row, at + 1U, title, ORFS_C_TITLE);
        orfs_term_put_at(t, row, at + 1U + len, ' ', ORFS_C_TITLE);
    }
}

/* One item: its marker, its tag with the first letter lit, its note. */
static void draw_item(const struct orfs_ui *ui, struct orfs_term *t,
                      uint32_t index, uint32_t row, uint32_t col,
                      uint32_t width)
{
    const struct orfs_ui_item *it = &ui->item[index];
    /* The highlight stays on the chosen item while the focus is down
     * on the buttons: pressing OK is about to act on it, so hiding
     * which one it is would be hiding the only thing that matters. */
    bool sel = index == ui->cursor;
    uint8_t body = sel ? ORFS_C_ITEM_SEL : ORFS_C_ITEM;
    uint8_t key = sel ? ORFS_C_KEY_SEL : ORFS_C_KEY;
    uint32_t at = col;

    orfs_term_repeat_at(t, row, col, ' ', width, body);
    if (ui->kind == ORFS_UI_CHECK) {
        orfs_term_put_at(t, row, at, '[', body);
        orfs_term_put_at(t, row, at + 1U, it->on ? 'X' : ' ', body);
        orfs_term_put_at(t, row, at + 2U, ']', body);
        at += 4U;
    } else if (ui->kind == ORFS_UI_RADIO) {
        orfs_term_put_at(t, row, at, '(', body);
        orfs_term_put_at(t, row, at + 1U, it->on ? '*' : ' ', body);
        orfs_term_put_at(t, row, at + 2U, ')', body);
        at += 4U;
    }
    orfs_term_puts_at(t, row, at, it->tag, body);
    /* The lit letter is the one you can press. A tag that starts with a
     * space is indented under the one above it and has no such letter,
     * so nothing is lit and nothing is promised. */
    if (it->tag[0] > ' ') {
        orfs_term_put_at(t, row, at, it->tag[0], key);
    }
    if (it->desc[0] != '\0') {
        orfs_term_puts_at(t, row, at + tag_width(ui) + 2U, it->desc, body);
    }
}

static void draw_buttons(const struct orfs_ui *ui, struct orfs_term *t,
                         uint32_t row, uint32_t col, uint32_t cols)
{
    uint32_t bw = button_width(ui);
    uint32_t at = col + orfs_ui_centre(button_row_width(ui), cols);

    for (uint32_t i = 0U; i < ui->buttons; ++i) {
        const char *label = ui->button[i];
        uint32_t len = orfs_strlen(label);
        bool sel = ui->on_buttons && i == ui->chosen;
        uint8_t body = sel ? ORFS_C_ITEM_SEL : ORFS_C_ITEM;
        uint8_t key = sel ? ORFS_C_KEY_SEL : ORFS_C_KEY;
        uint32_t text_at = at + 1U + orfs_ui_centre(len, bw - 2U);

        orfs_term_put_at(t, row, at, '<', ORFS_C_BOX);
        orfs_term_repeat_at(t, row, at + 1U, ' ', bw - 2U, body);
        orfs_term_puts_at(t, row, text_at, label, body);
        if (len != 0U) {
            orfs_term_put_at(t, row, text_at, label[0], key);
        }
        orfs_term_put_at(t, row, at + bw - 1U, '>', ORFS_C_BOX);
        at += bw + 2U;
    }
}

static void draw_field(const struct orfs_ui *ui, struct orfs_term *t,
                       uint32_t index, uint32_t row, uint32_t col)
{
    const struct orfs_ui_field *f = &ui->field[index];
    uint32_t lw = label_width(ui);
    uint32_t box = col + lw + 1U;
    uint32_t len = orfs_strlen(f->value);

    orfs_term_puts_at(t, row, col, f->label, ORFS_C_ITEM);
    orfs_term_repeat_at(t, row, box, ' ', f->width, ORFS_C_INPUT);
    if (f->secret) {
        /* Typed, counted, never shown. */
        orfs_term_repeat_at(t, row, box, '*', umin(len, f->width),
                            ORFS_C_INPUT);
    } else {
        orfs_term_puts_at(t, row, box, f->value, ORFS_C_INPUT);
    }
    /* The caret is a block, on the field being typed into, and only on
     * that one - two carets would be two places to type. */
    if (index == ui->focus && !ui->on_buttons) {
        uint32_t at = box + umin(ui->caret, f->width - 1U);

        orfs_term_put_at(t, row, at, orfs_term_at(t, row, at),
                         ORFS_ATTR(ORFS_BLUE, ORFS_WHITE));
    }
}

static void draw_gauge(const struct orfs_ui *ui, struct orfs_term *t,
                       uint32_t row, uint32_t col, uint32_t width)
{
    uint32_t done = (width * ui->percent) / 100U;
    char pc[8];
    uint32_t len;
    uint32_t at;

    /* A trough, and a bar filling it. */
    orfs_term_repeat_at(t, row, col, ORFS_G_SHADE, width,
                        ORFS_ATTR(ORFS_DARK, ORFS_GREY));
    orfs_term_repeat_at(t, row, col, ORFS_G_BLOCK, done, ORFS_C_GAUGE);

    (void)orfs_u32(pc, sizeof(pc), ui->percent);
    (void)orfs_strcat(pc, sizeof(pc), "%");
    len = orfs_strlen(pc);
    at = col + orfs_ui_centre(len, width);
    /*
     * The number sits on the bar rather than under it, and each of its
     * characters takes the colour of whichever side of the bar it is
     * on, so the reading changes as the bar passes through it. That is
     * where a gauge has kept its number since there were gauges.
     */
    for (uint32_t i = 0U; i < len; ++i) {
        bool filled = at + i < col + done;

        orfs_term_put_at(t, row, at + i, pc[i],
                         filled ? ORFS_ATTR(ORFS_WHITE, ORFS_HIBLUE)
                                : ORFS_ATTR(ORFS_BLACK, ORFS_GREY));
    }
}

void orfs_ui_draw(const struct orfs_ui *ui, struct orfs_term *t)
{
    uint32_t inner;
    uint32_t cols;
    uint32_t rows;
    uint32_t text_lines;
    uint32_t body_rows;
    uint32_t row;
    uint32_t col;
    uint32_t at;
    char line[ORFS_COLS + 1];

    if (ui == NULL || t == NULL) {
        return;
    }

    /* Width comes from the widest thing in the box. */
    cols = BOX_MIN;
    cols = umax(cols, orfs_strlen(ui->title) + 8U);
    cols = umax(cols, item_width(ui) + 2U * PAD + 2U);
    cols = umax(cols, form_width(ui) + 2U * PAD + 2U);
    cols = umax(cols, button_row_width(ui) + 2U * PAD + 2U);
    /* The line under a gauge is as much a part of it as the bar. */
    cols = umax(cols, orfs_strlen(ui->note) + 2U * PAD + 2U);
    if (ui->text[0] != '\0') {
        cols = umax(cols, 60U);
    }
    cols = umin(cols, BOX_MAX);
    inner = cols - 2U * PAD;

    text_lines = wrap(ui->text, inner, (uint32_t)-1, NULL, 0U);
    if (ui->kind == ORFS_UI_FORM) {
        body_rows = ui->fields;
    } else if (ui->kind == ORFS_UI_GAUGE) {
        /* The bar, then a blank, then the line saying what it is
         * doing - and nothing at all when there is no such line. */
        body_rows = 1U + (ui->note[0] != '\0' ? 2U : 0U);
    } else {
        body_rows = ui->visible;
    }

    /* 2 borders, a blank line under the text, a blank line over the
     * buttons, and the button row itself. */
    rows = 2U + (text_lines != 0U ? text_lines + 1U : 0U) + body_rows;
    rows += ui->buttons != 0U ? 2U : 0U;
    if (rows > ORFS_ROWS - 4U) {
        rows = ORFS_ROWS - 4U;
    }

    col = orfs_ui_centre(cols, ORFS_COLS);
    row = 2U + orfs_ui_centre(rows, ORFS_ROWS - 4U);

    orfs_ui_frame(t, row, col, rows, cols, ui->title);

    at = row + 1U;
    for (uint32_t i = 0U; i < text_lines; ++i) {
        (void)wrap(ui->text, inner, i, line, sizeof(line));
        orfs_term_puts_at(t, at, col + PAD, line, ORFS_C_ITEM);
        ++at;
    }
    if (text_lines != 0U) {
        ++at;
    }

    if (ui->kind == ORFS_UI_FORM) {
        for (uint32_t i = 0U; i < ui->fields; ++i) {
            draw_field(ui, t, i, at + i, col + PAD);
        }
        at += ui->fields;
    } else if (ui->kind == ORFS_UI_GAUGE) {
        draw_gauge(ui, t, at, col + PAD, inner);
        ++at;
        if (ui->note[0] != '\0') {
            ++at;
            orfs_term_puts_at(t, at, col + PAD, ui->note, ORFS_C_ITEM);
            ++at;
        }
    } else {
        uint32_t shown = umin(ui->visible, ui->items);

        for (uint32_t i = 0U; i < shown; ++i) {
            draw_item(ui, t, ui->top + i, at + i, col + PAD, inner);
        }
        /* A list longer than its window says which way the rest is. */
        if (ui->top != 0U) {
            orfs_term_put_at(t, at, col + cols - 2U, ORFS_G_UP,
                             ORFS_C_EDGE_LO);
        }
        if (ui->top + shown < ui->items) {
            orfs_term_put_at(t, at + shown - 1U, col + cols - 2U,
                             ORFS_G_DOWN, ORFS_C_EDGE_LO);
        }
        at += shown;
    }

    if (ui->buttons != 0U) {
        draw_buttons(ui, t, row + rows - 2U, col, cols);
    }
    t->cursor = false;
}
