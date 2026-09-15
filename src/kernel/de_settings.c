/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/de/settings.h>

#include <trait/de/font.h>
#include <trait/de/files.h>
#include <trait/de/theme.h>

#define TAB_HEIGHT 24U
#define TAB_PAD 12U
#define PAGE_PAD 10U
#define ROW_HEIGHT 30U
#define SWITCH_BOX 13U

static struct trait_settings_page pages[TRAIT_SETTINGS_MAX_PAGES];
static uint32_t page_count;
static uint32_t current;

static void copy(char *out, const char *text, uint32_t capacity)
{
    uint32_t at = 0U;

    while (text != NULL && text[at] != '\0' && at + 1U < capacity) {
        out[at] = text[at];
        ++at;
    }
    out[at] = '\0';
}

void trait_settings_reset(void)
{
    page_count = 0U;
    current = 0U;
}

bool trait_settings_add_page(const char *name)
{
    if (name == NULL || page_count >= TRAIT_SETTINGS_MAX_PAGES) {
        return false;
    }
    copy(pages[page_count].name, name, TRAIT_SETTINGS_TEXT_BYTES);
    pages[page_count].row_count = 0U;
    ++page_count;
    return true;
}

bool trait_settings_add_row(uint32_t page,
    const struct trait_settings_row *row)
{
    struct trait_settings_page *target;

    if (row == NULL || page >= page_count) {
        return false;
    }
    target = &pages[page];
    if (target->row_count >= TRAIT_SETTINGS_MAX_ROWS) {
        return false;
    }
    target->rows[target->row_count] = *row;
    copy(target->rows[target->row_count].label, row->label,
         TRAIT_SETTINGS_TEXT_BYTES);
    copy(target->rows[target->row_count].value, row->value,
         TRAIT_SETTINGS_TEXT_BYTES);
    ++target->row_count;
    return true;
}

uint32_t trait_settings_page_count(void)
{
    return page_count;
}

void trait_settings_select(uint32_t page)
{
    if (page < page_count) {
        current = page;
    }
}

uint32_t trait_settings_selected(void)
{
    return current;
}

bool trait_settings_tab_bounds(const struct trait_window *window,
    uint32_t page, struct trait_rect *out)
{
    struct trait_rect client;
    uint32_t left;
    uint32_t at;

    if (window == NULL || out == NULL || page >= page_count) {
        return false;
    }
    client = trait_window_client(window);
    left = client.x + 4U;
    for (at = 0U; at < page; ++at) {
        left += trait_font_width(pages[at].name) + TAB_PAD * 2U;
    }
    out->x = left;
    out->y = client.y + 4U;
    out->width = trait_font_width(pages[page].name) + TAB_PAD * 2U;
    out->height = TAB_HEIGHT;
    return true;
}

/*
 * GTK's notebook: the current tab is the page's own colour and JOINS the
 * page - there is no line under it - while the others sit lower and
 * darker with the line running over them.  That join is the whole of how
 * a notebook says which page you are looking at, so it is drawn rather
 * than approximated with a highlight.
 */
static void draw_tabs(struct trait_surface *surface,
    const struct trait_window *window, struct trait_rect body)
{
    struct trait_rect tab;
    uint32_t at;
    uint32_t edge;

    for (at = 0U; at < page_count; ++at) {
        bool here = at == current;

        if (!trait_settings_tab_bounds(window, at, &tab)) {
            continue;
        }
        if (!here) {
            tab.y += 3U;
            tab.height -= 3U;
        }
        trait_surface_fill(surface, body, tab,
                           here ? TRAIT_BG : TRAIT_BG_ACTIVE);
        for (edge = 0U; edge < tab.width; ++edge) {
            trait_surface_plot(surface, body, tab.x + edge, tab.y,
                               TRAIT_LINE);
        }
        for (edge = 0U; edge < tab.height; ++edge) {
            trait_surface_plot(surface, body, tab.x, tab.y + edge,
                               TRAIT_LINE);
            trait_surface_plot(surface, body, tab.x + tab.width - 1U,
                               tab.y + edge, TRAIT_LINE);
        }
        trait_font_draw(surface, body, tab.x + TAB_PAD,
            tab.y + tab.height - 8U, pages[at].name,
            here ? TRAIT_FG : TRAIT_TEXT);
    }
}

bool trait_settings_row_bounds(const struct trait_window *window,
    uint32_t row, struct trait_rect *out)
{
    struct trait_rect client;

    if (window == NULL || out == NULL || current >= page_count ||
            row >= pages[current].row_count) {
        return false;
    }
    client = trait_window_client(window);
    out->x = client.x + 4U + PAGE_PAD;
    out->y = client.y + 4U + TAB_HEIGHT + PAGE_PAD + row * ROW_HEIGHT;
    out->width = client.width > 8U + PAGE_PAD * 2U ?
        client.width - 8U - PAGE_PAD * 2U : 0U;
    out->height = ROW_HEIGHT;
    return true;
}

/*
 * The value a row SHOWS is read from the thing it controls, not stored
 * beside it.  A settings window that keeps its own copy of the state
 * drifts from it - the page says Clearlooks while the desktop is dark -
 * and that is worse than a page that cannot change anything at all.
 */
static void refresh(struct trait_settings_row *row)
{
    switch (row->setting) {
    case TRAIT_SET_WIDGET_THEME:
        copy(row->value, trait_theme_name(trait_theme_selected()),
             TRAIT_SETTINGS_TEXT_BYTES);
        break;
    case TRAIT_SET_FILES_VIEW:
        copy(row->value,
             trait_files_view_mode() == TRAIT_FILES_LIST ?
                 "Detailed list" : "Icons",
             TRAIT_SETTINGS_TEXT_BYTES);
        break;
    case TRAIT_SET_DESKTOP_ICONS:
    case TRAIT_SET_SHOW_HIDDEN:
    case TRAIT_SET_NOTHING:
    default:
        break;
    }
}

bool trait_settings_press(uint32_t page, uint32_t row)
{
    struct trait_settings_row *target;

    if (page >= page_count || row >= pages[page].row_count) {
        return false;
    }
    target = &pages[page].rows[row];
    switch (target->setting) {
    case TRAIT_SET_WIDGET_THEME: {
        uint32_t next = trait_theme_selected() + 1U;

        if (next >= trait_theme_count()) {
            next = 0U;
        }
        if (!trait_theme_select(next)) {
            return false;
        }
        refresh(target);
        return true;
    }
    case TRAIT_SET_FILES_VIEW:
        trait_files_set_view(
            trait_files_view_mode() == TRAIT_FILES_LIST ?
                TRAIT_FILES_ICONS : TRAIT_FILES_LIST);
        refresh(target);
        return true;
    case TRAIT_SET_DESKTOP_ICONS:
    case TRAIT_SET_SHOW_HIDDEN:
        target->on = !target->on;
        return true;
    case TRAIT_SET_NOTHING:
    default:
        /* A note is not a control.  Pressing one does nothing and says
         * so, rather than swallowing the press. */
        return false;
    }
}

void trait_settings_draw(struct trait_surface *surface,
    const struct trait_window *window)
{
    struct trait_rect client;
    struct trait_rect body;
    struct trait_settings_page *page;
    uint32_t at;
    uint32_t edge;
    uint32_t top;

    if (window == NULL || !trait_surface_valid(surface) ||
            page_count == 0U) {
        return;
    }
    client = trait_window_client(window);
    trait_surface_fill(surface, client, client, TRAIT_BG);

    body.x = client.x + 4U;
    body.y = client.y + 4U + TAB_HEIGHT;
    body.width = client.width > 8U ? client.width - 8U : 0U;
    body.height = client.height > TAB_HEIGHT + 12U ?
        client.height - TAB_HEIGHT - 12U : 0U;
    trait_surface_fill(surface, client, body, TRAIT_BG);
    for (edge = 0U; edge < body.width; ++edge) {
        trait_surface_plot(surface, client, body.x + edge, body.y,
                           TRAIT_LINE);
        trait_surface_plot(surface, client, body.x + edge,
                           body.y + body.height - 1U, TRAIT_LINE);
    }
    for (edge = 0U; edge < body.height; ++edge) {
        trait_surface_plot(surface, client, body.x, body.y + edge,
                           TRAIT_LINE);
        trait_surface_plot(surface, client, body.x + body.width - 1U,
                           body.y + edge, TRAIT_LINE);
    }
    draw_tabs(surface, window, client);

    page = &pages[current];
    for (at = 0U; at < page->row_count; ++at) {
        refresh(&page->rows[at]);
    }
    top = body.y + PAGE_PAD;
    for (at = 0U; at < page->row_count; ++at) {
        const struct trait_settings_row *row = &page->rows[at];
        uint32_t baseline = top + 12U;

        if (top + ROW_HEIGHT > body.y + body.height) {
            break;
        }
        switch (row->kind) {
        case TRAIT_SETTINGS_SWITCH: {
            struct trait_rect box;

            box.x = body.x + PAGE_PAD;
            box.y = top + 2U;
            box.width = SWITCH_BOX;
            box.height = SWITCH_BOX;
            trait_surface_fill(surface, body, box, TRAIT_BASE);
            for (edge = 0U; edge < SWITCH_BOX; ++edge) {
                trait_surface_plot(surface, body, box.x + edge, box.y,
                                   TRAIT_LINE);
                trait_surface_plot(surface, body, box.x + edge,
                    box.y + SWITCH_BOX - 1U, TRAIT_LINE);
                trait_surface_plot(surface, body, box.x, box.y + edge,
                                   TRAIT_LINE);
                trait_surface_plot(surface, body,
                    box.x + SWITCH_BOX - 1U, box.y + edge, TRAIT_LINE);
            }
            if (row->on) {
                /* A tick, drawn: two strokes, the short one up from the
                 * bottom left and the long one down from the top right. */
                for (edge = 0U; edge < 3U; ++edge) {
                    trait_surface_plot(surface, body, box.x + 3U + edge,
                        box.y + 6U + edge, TRAIT_FG);
                }
                for (edge = 0U; edge < 5U; ++edge) {
                    trait_surface_plot(surface, body, box.x + 6U + edge,
                        box.y + 8U - edge, TRAIT_FG);
                }
            }
            trait_font_draw(surface, body,
                body.x + PAGE_PAD + SWITCH_BOX + 7U, baseline,
                row->label, TRAIT_FG);
            break;
        }
        case TRAIT_SETTINGS_NOTE:
            trait_font_draw(surface, body, body.x + PAGE_PAD, baseline,
                            row->label, TRAIT_TEXT);
            break;
        case TRAIT_SETTINGS_CHOICE:
        default: {
            struct trait_rect box;
            uint32_t width;

            trait_font_draw(surface, body, body.x + PAGE_PAD, baseline,
                            row->label, TRAIT_FG);
            width = trait_font_width(row->value) + 26U;
            box.width = width < 110U ? 110U : width;
            box.height = 20U;
            box.x = body.x + body.width - box.width - PAGE_PAD;
            box.y = top;
            trait_surface_fill(surface, body, box, TRAIT_BG);
            for (edge = 0U; edge < box.width; ++edge) {
                trait_surface_plot(surface, body, box.x + edge, box.y,
                                   TRAIT_LINE_LIGHT);
                trait_surface_plot(surface, body, box.x + edge,
                    box.y + box.height - 1U, TRAIT_LINE);
            }
            for (edge = 0U; edge < box.height; ++edge) {
                trait_surface_plot(surface, body, box.x, box.y + edge,
                                   TRAIT_LINE_LIGHT);
                trait_surface_plot(surface, body,
                    box.x + box.width - 1U, box.y + edge, TRAIT_LINE);
            }
            trait_font_draw(surface, box, box.x + 6U, box.y + 14U,
                            row->value, TRAIT_TEXT);
            /* The drop arrow, which is what says this is a chooser and
             * not a box somebody typed in. */
            for (edge = 0U; edge < 4U; ++edge) {
                uint32_t span;

                for (span = 0U; span + edge < 4U; ++span) {
                    trait_surface_plot(surface, box,
                        box.x + box.width - 13U + edge + span,
                        box.y + 8U + edge, TRAIT_FG);
                }
            }
            break;
        }
        }
        top += ROW_HEIGHT;
    }
}

/*
 * The self test asks the one thing a notebook has to get right: does
 * picking a tab actually change which page is drawn?  A notebook whose
 * tabs only move a highlight is a picture of a notebook.
 */
bool trait_settings_self_test(void)
{
    struct trait_settings_row row;

    trait_settings_reset();
    if (!trait_settings_add_page("Widget") ||
            !trait_settings_add_page("Desktop")) {
        return false;
    }
    copy(row.label, "Widget theme", TRAIT_SETTINGS_TEXT_BYTES);
    copy(row.value, "Clearlooks", TRAIT_SETTINGS_TEXT_BYTES);
    row.kind = TRAIT_SETTINGS_CHOICE;
    row.on = false;
    row.setting = TRAIT_SET_WIDGET_THEME;
    if (!trait_settings_add_row(0U, &row)) {
        return false;
    }
    copy(row.label, "Show icons on the desktop", TRAIT_SETTINGS_TEXT_BYTES);
    copy(row.value, "", TRAIT_SETTINGS_TEXT_BYTES);
    row.kind = TRAIT_SETTINGS_SWITCH;
    row.on = true;
    row.setting = TRAIT_SET_DESKTOP_ICONS;
    if (!trait_settings_add_row(1U, &row)) {
        return false;
    }
    if (trait_settings_selected() != 0U) {
        return false;
    }
    trait_settings_select(1U);
    if (trait_settings_selected() != 1U) {
        return false;
    }
    /* A page that does not exist is REFUSED rather than selected: a tab
     * index off the end would draw whatever was in memory. */
    trait_settings_select(9U);
    if (trait_settings_selected() != 1U) {
        return false;
    }

    /*
     * And the rows CHANGE THINGS.  Pressing the widget-theme row must
     * move the palette, not just the word on the page - so this reads the
     * colour the rest of the desktop draws with, not the row's own text.
     */
    {
        uint32_t was_theme = trait_theme_selected();
        uint32_t was_bg = TRAIT_BG;

        if (!trait_settings_press(0U, 0U)) {
            return false;
        }
        if (trait_theme_selected() == was_theme) {
            return false;
        }
        if (TRAIT_BG == was_bg) {
            return false;
        }
        /* The page now SAYS what the desktop IS, because the value is
         * read from the theme rather than stored beside it. */
        {
            const char *shown = pages[0].rows[0].value;
            const char *real = trait_theme_name(trait_theme_selected());
            uint32_t byte = 0U;

            while (shown[byte] != '\0' && real[byte] != '\0' &&
                    shown[byte] == real[byte]) {
                ++byte;
            }
            if (shown[byte] != real[byte]) {
                return false;
            }
        }
        /* It WRAPS rather than stopping at the last theme. */
        while (trait_theme_selected() != was_theme) {
            if (!trait_settings_press(0U, 0U)) {
                return false;
            }
        }
        if (TRAIT_BG != was_bg) {
            return false;
        }
        /* A switch flips. */
        if (!trait_settings_press(1U, 0U)) {
            return false;
        }
        if (pages[1].rows[0].on) {
            return false;
        }
        /* A row that is not a control refuses the press rather than
         * swallowing it. */
        copy(row.label, "A note", TRAIT_SETTINGS_TEXT_BYTES);
        row.kind = TRAIT_SETTINGS_NOTE;
        row.setting = TRAIT_SET_NOTHING;
        if (!trait_settings_add_row(1U, &row)) {
            return false;
        }
        if (trait_settings_press(1U, 1U)) {
            return false;
        }
    }
    trait_settings_reset();
    return true;
}
