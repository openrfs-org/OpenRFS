/* SPDX-License-Identifier: GPL-3.0-only */
#include <opengat/de/taskmgr.h>

#include <opengat/de/font.h>
#include <opengat/de/theme.h>

/* ================================================================ METRICS */

#define TASKMGR_MENUBAR 20U
#define TASKMGR_SUMMARY 20U
#define TASKMGR_HEADER 18U
#define TASKMGR_ROW 17U
#define TASKMGR_PAD 6U
#define TASKMGR_FOOTER 32U

/*
 * The columns.  Command takes what is left, which is right: it is the one
 * that varies in length and the one worth reading.  The rest are sized to
 * their longest plausible value and RIGHT ALIGNED, because a column of
 * numbers that is not right aligned cannot be scanned.
 */
static const uint32_t COLUMN_WIDTH[OPENGAT_TASKMGR_COLUMNS] = {
    0U, 70U, 60U, 76U, 50U
};

static const char *const COLUMN_NAME[OPENGAT_TASKMGR_COLUMNS] = {
    "Command", "User", "CPU%", "RSS", "PID"
};

/* ================================================================== STATE */

static struct opengat_taskmgr_row rows[OPENGAT_TASKMGR_MAX_ROWS];
static uint32_t row_count;
static enum opengat_taskmgr_column sort_column = OPENGAT_TASKMGR_PID;
static bool sort_descending;
static uint32_t chosen = OPENGAT_TASKMGR_MAX_ROWS;

/* ================================================================ HELPERS */

static void copy(char *out, const char *text, uint32_t capacity)
{
    uint32_t at = 0U;

    while (text != NULL && text[at] != '\0' && at + 1U < capacity) {
        out[at] = text[at];
        ++at;
    }
    out[at] = '\0';
}

static int compare_text(const char *a, const char *b)
{
    uint32_t at = 0U;

    while (a[at] != '\0' && b[at] != '\0') {
        if (a[at] != b[at]) {
            return a[at] < b[at] ? -1 : 1;
        }
        ++at;
    }
    if (a[at] == b[at]) {
        return 0;
    }
    return a[at] == '\0' ? -1 : 1;
}

static int compare(const struct opengat_taskmgr_row *a,
    const struct opengat_taskmgr_row *b)
{
    switch (sort_column) {
    case OPENGAT_TASKMGR_COMMAND:
        return compare_text(a->command, b->command);
    case OPENGAT_TASKMGR_USER:
        return compare_text(a->user, b->user);
    case OPENGAT_TASKMGR_CPU:
        if (a->cpu_tenths == b->cpu_tenths) {
            return 0;
        }
        return a->cpu_tenths < b->cpu_tenths ? -1 : 1;
    case OPENGAT_TASKMGR_RSS:
        if (a->rss_kib == b->rss_kib) {
            return 0;
        }
        return a->rss_kib < b->rss_kib ? -1 : 1;
    case OPENGAT_TASKMGR_PID:
    default:
        if (a->pid == b->pid) {
            return 0;
        }
        return a->pid < b->pid ? -1 : 1;
    }
}

/* Insertion sort: the list is a couple of dozen rows and it is STABLE,
 * which matters - sorting by User must leave equal users in the order
 * they were in rather than shuffling them every redraw. */
static void resort(void)
{
    uint32_t at;
    const bool keep_selection = chosen < row_count;
    const uint32_t selected_pid = keep_selection ? rows[chosen].pid : 0U;

    for (at = 1U; at < row_count; ++at) {
        struct opengat_taskmgr_row held = rows[at];
        uint32_t back = at;

        while (back > 0U) {
            int order = compare(&rows[back - 1U], &held);

            if (sort_descending) {
                order = -order;
            }
            if (order <= 0) {
                break;
            }
            rows[back] = rows[back - 1U];
            --back;
        }
        rows[back] = held;
    }
    if (!keep_selection) {
        return;
    }
    chosen = OPENGAT_TASKMGR_MAX_ROWS;
    for (at = 0U; at < row_count; ++at) {
        if (rows[at].pid == selected_pid) {
            chosen = at;
            break;
        }
    }
}

/* Digits, without libc.  `tenths` prints 42 as "4.2". */
static uint32_t number(char *out, uint32_t value, uint32_t capacity)
{
    char digits[12];
    uint32_t length = 0U;
    uint32_t at = 0U;

    if (value == 0U) {
        digits[length++] = '0';
    }
    while (value != 0U && length < sizeof(digits)) {
        digits[length++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (length != 0U && at + 1U < capacity) {
        out[at++] = digits[--length];
    }
    out[at] = '\0';
    return at;
}

static void tenths(char *out, uint32_t value, uint32_t capacity)
{
    uint32_t at = number(out, value / 10U, capacity);

    if (at + 2U < capacity) {
        out[at++] = '.';
        out[at++] = (char)('0' + (value % 10U));
        out[at] = '\0';
    }
}

/* KiB as the unit the value is actually in, which is what lxtask does
 * rather than inventing a precision it has not got. */
static void memory(char *out, uint32_t kib, uint32_t capacity)
{
    if (kib >= 1024U) {
        uint32_t mib = kib * 10U / 1024U;
        uint32_t at = number(out, mib / 10U, capacity);

        if (at + 6U < capacity) {
            out[at++] = '.';
            out[at++] = (char)('0' + (mib % 10U));
            out[at++] = ' ';
            out[at++] = 'M';
            out[at++] = 'i';
            out[at++] = 'B';
            out[at] = '\0';
        }
        return;
    }
    {
        uint32_t at = number(out, kib, capacity);

        if (at + 4U < capacity) {
            out[at++] = ' ';
            out[at++] = 'K';
            out[at++] = 'i';
            out[at++] = 'B';
            out[at] = '\0';
        }
    }
}

/* ================================================================== API */

void opengat_taskmgr_reset(void)
{
    row_count = 0U;
    chosen = OPENGAT_TASKMGR_MAX_ROWS;
}

bool opengat_taskmgr_add(const struct opengat_taskmgr_row *row)
{
    if (row == NULL || row_count >= OPENGAT_TASKMGR_MAX_ROWS) {
        return false;
    }
    rows[row_count] = *row;
    copy(rows[row_count].command, row->command,
         OPENGAT_TASKMGR_NAME_BYTES);
    copy(rows[row_count].user, row->user, OPENGAT_TASKMGR_NAME_BYTES);
    ++row_count;
    resort();
    return true;
}

uint32_t opengat_taskmgr_count(void)
{
    return row_count;
}

void opengat_taskmgr_select(uint32_t at)
{
    if (at < row_count) {
        chosen = at;
    }
}

uint32_t opengat_taskmgr_selected(void)
{
    return chosen;
}

bool opengat_taskmgr_has_selection(void)
{
    return chosen < row_count;
}

uint32_t opengat_taskmgr_selected_pid(void)
{
    return chosen < row_count ? rows[chosen].pid : 0U;
}

bool opengat_taskmgr_end_selected(void)
{
    uint32_t at;

    if (chosen >= row_count) {
        return false;
    }
    /* pid 1 is the session.  A task manager that lets you end the thing
     * it is running inside is offering to turn the screen off. */
    if (rows[chosen].pid == 1U) {
        return false;
    }
    for (at = chosen; at + 1U < row_count; ++at) {
        rows[at] = rows[at + 1U];
    }
    --row_count;
    /* The selection does NOT follow the gap onto whatever moved up: the
     * next press would then end a row nobody chose. */
    chosen = OPENGAT_TASKMGR_MAX_ROWS;
    return true;
}

bool opengat_taskmgr_row_bounds(const struct opengat_window *window,
    uint32_t at, struct opengat_rect *out)
{
    struct opengat_rect client;
    const uint32_t list_offset = TASKMGR_MENUBAR + TASKMGR_SUMMARY +
        TASKMGR_HEADER;
    uint32_t list_height;

    if (window == NULL || out == NULL || at >= row_count) {
        return false;
    }
    client = opengat_window_client(window);
    list_height = client.height > list_offset + TASKMGR_FOOTER ?
        client.height - list_offset - TASKMGR_FOOTER : 0U;
    if ((at + 1U) * TASKMGR_ROW > list_height) {
        return false;
    }
    out->x = client.x;
    out->y = client.y + list_offset + at * TASKMGR_ROW;
    out->width = client.width;
    out->height = TASKMGR_ROW;
    return true;
}

bool opengat_taskmgr_end_button(const struct opengat_window *window,
    struct opengat_rect *out)
{
    struct opengat_rect client;

    if (window == NULL || out == NULL) {
        return false;
    }
    client = opengat_window_client(window);
    out->width = 82U;
    out->height = 22U;
    out->x = client.x + client.width - out->width - TASKMGR_PAD;
    out->y = client.y + client.height - out->height - 5U;
    return true;
}

void opengat_taskmgr_sort(enum opengat_taskmgr_column column)
{
    if (column == sort_column) {
        sort_descending = !sort_descending;
    } else {
        sort_column = column;
        sort_descending = false;
    }
    resort();
}

enum opengat_taskmgr_column opengat_taskmgr_sort_column(void)
{
    return sort_column;
}

bool opengat_taskmgr_sort_descending(void)
{
    return sort_descending;
}

/* ================================================================ LAYOUT */

static uint32_t command_width(struct opengat_rect client)
{
    uint32_t fixed = 0U;
    uint32_t at;

    for (at = 1U; at < OPENGAT_TASKMGR_COLUMNS; ++at) {
        fixed += COLUMN_WIDTH[at];
    }
    if (client.width < fixed + TASKMGR_PAD * 2U + 60U) {
        return 60U;
    }
    return client.width - fixed - TASKMGR_PAD * 2U;
}

bool opengat_taskmgr_header_bounds(const struct opengat_window *window,
    enum opengat_taskmgr_column column, struct opengat_rect *out)
{
    struct opengat_rect client;
    uint32_t left;
    uint32_t at;

    if (window == NULL || out == NULL ||
            (uint32_t)column >= OPENGAT_TASKMGR_COLUMNS) {
        return false;
    }
    client = opengat_window_client(window);
    left = client.x + TASKMGR_PAD;
    for (at = 0U; at < (uint32_t)column; ++at) {
        left += at == 0U ? command_width(client) : COLUMN_WIDTH[at];
    }
    out->x = left;
    out->y = client.y + TASKMGR_MENUBAR + TASKMGR_SUMMARY;
    out->width = column == OPENGAT_TASKMGR_COMMAND ?
        command_width(client) : COLUMN_WIDTH[column];
    out->height = TASKMGR_HEADER;
    return true;
}

/* ================================================================ DRAWING */

static void draw_cell(struct opengat_surface *surface, struct opengat_rect clip,
    struct opengat_rect box, const char *text, uint32_t baseline,
    uint32_t ink, bool right_aligned)
{
    uint32_t width = opengat_font_width(text);
    uint32_t x = box.x + 4U;

    if (right_aligned && box.width > width + 8U) {
        x = box.x + box.width - width - 6U;
    }
    opengat_font_draw(surface, clip, x, baseline, text, ink);
}

void opengat_taskmgr_draw(struct opengat_surface *surface,
    const struct opengat_window *window)
{
    struct opengat_rect client;
    struct opengat_rect strip;
    char scratch[24];
    uint32_t at;
    uint32_t column;
    uint32_t list_top;

    if (window == NULL || !opengat_surface_valid(surface)) {
        return;
    }
    client = opengat_window_client(window);
    opengat_surface_fill(surface, client, client, OPENGAT_BG);

    /* the menu bar */
    strip = client;
    strip.height = TASKMGR_MENUBAR;
    opengat_surface_fill(surface, client, strip, OPENGAT_BG);
    {
        static const char *const MENUS[3] = { "File", "View", "Help" };
        uint32_t pen = client.x + TASKMGR_PAD;

        for (at = 0U; at < 3U; ++at) {
            opengat_font_draw(surface, client, pen,
                client.y + 14U, MENUS[at], OPENGAT_FG);
            pen += opengat_font_width(MENUS[at]) + 14U;
        }
    }
    for (at = 0U; at < client.width; ++at) {
        opengat_surface_plot(surface, client, client.x + at,
            client.y + TASKMGR_MENUBAR - 1U, OPENGAT_LINE);
    }

    /* the summary line */
    {
        uint32_t pen = client.x + TASKMGR_PAD;
        uint32_t base = client.y + TASKMGR_MENUBAR + 14U;

        opengat_font_draw(surface, client, pen, base, "Tasks:", OPENGAT_TEXT);
        pen += opengat_font_width("Tasks:") + 5U;
        (void)number(scratch, row_count, sizeof(scratch));
        opengat_font_draw(surface, client, pen, base, scratch, OPENGAT_TEXT);
    }

    /* the column header, with the sorted one marked */
    for (column = 0U; column < OPENGAT_TASKMGR_COLUMNS; ++column) {
        struct opengat_rect head;

        if (!opengat_taskmgr_header_bounds(window,
                (enum opengat_taskmgr_column)column, &head)) {
            continue;
        }
        opengat_surface_fill(surface, client, head, OPENGAT_BG_ACTIVE);
        for (at = 0U; at < head.height; ++at) {
            opengat_surface_plot(surface, client,
                head.x + head.width - 1U, head.y + at, OPENGAT_LINE);
        }
        for (at = 0U; at < head.width; ++at) {
            opengat_surface_plot(surface, client, head.x + at,
                head.y + head.height - 1U, OPENGAT_LINE);
        }
        opengat_font_draw(surface, head, head.x + 4U, head.y + 13U,
            COLUMN_NAME[column], OPENGAT_FG);
        if ((enum opengat_taskmgr_column)column == sort_column &&
                head.width > 20U) {
            /* The sort marker: a triangle, pointing the way the list
             * runs.  Drawn rather than carried as a picture, because at
             * five pixels a picture is five lines with a file round it. */
            uint32_t tip = head.x + head.width - 10U;
            uint32_t mid = head.y + head.height / 2U;

            /*
             * ASCENDING POINTS UP, descending points down, which is the
             * way round every list with a sortable header does it.  The
             * first cut had it inverted - a list running largest-first
             * under an upward arrow - which is worse than no marker,
             * because a marker that is wrong is still believed.
             */
            for (at = 0U; at < 4U; ++at) {
                uint32_t span;

                for (span = 0U; span <= at; ++span) {
                    uint32_t row = sort_descending ?
                        mid + 2U - at : mid - 2U + at;

                    opengat_surface_plot(surface, head, tip + span, row,
                                       OPENGAT_FG);
                    opengat_surface_plot(surface, head, tip - span, row,
                                       OPENGAT_FG);
                }
            }
        }
    }

    /* the rows */
    list_top = client.y + TASKMGR_MENUBAR + TASKMGR_SUMMARY +
        TASKMGR_HEADER;
    {
        struct opengat_rect list;

        list.x = client.x;
        list.y = list_top;
        list.width = client.width;
        list.height = client.height > (list_top - client.y) +
            TASKMGR_FOOTER ?
            client.height - (list_top - client.y) - TASKMGR_FOOTER : 0U;
        opengat_surface_fill(surface, client, list, OPENGAT_BASE);

        for (at = 0U; at < row_count; ++at) {
            uint32_t top = list_top + at * TASKMGR_ROW;
            uint32_t baseline = top + 12U;
            uint32_t ink = at == chosen ? OPENGAT_SEL_FG : OPENGAT_TEXT;

            if (top + TASKMGR_ROW > list.y + list.height) {
                break;
            }
            if (at == chosen) {
                struct opengat_rect band;

                band.x = list.x;
                band.y = top;
                band.width = list.width;
                band.height = TASKMGR_ROW;
                opengat_surface_fill(surface, list, band, OPENGAT_SEL_BG);
            } else if ((at & 1U) != 0U) {
                struct opengat_rect band;

                band.x = list.x;
                band.y = top;
                band.width = list.width;
                band.height = TASKMGR_ROW;
                opengat_surface_fill(surface, list, band,
                                   OPENGAT_BASE_PRELIGHT);
            }
            for (column = 0U; column < OPENGAT_TASKMGR_COLUMNS; ++column) {
                struct opengat_rect head;
                struct opengat_rect cell;

                if (!opengat_taskmgr_header_bounds(window,
                        (enum opengat_taskmgr_column)column, &head)) {
                    continue;
                }
                cell = head;
                cell.y = top;
                cell.height = TASKMGR_ROW;
                switch (column) {
                case OPENGAT_TASKMGR_COMMAND:
                    draw_cell(surface, list, cell, rows[at].command,
                              baseline, ink, false);
                    break;
                case OPENGAT_TASKMGR_USER:
                    draw_cell(surface, list, cell, rows[at].user,
                              baseline, ink, false);
                    break;
                case OPENGAT_TASKMGR_CPU:
                    tenths(scratch, rows[at].cpu_tenths, sizeof(scratch));
                    draw_cell(surface, list, cell, scratch, baseline,
                              ink, true);
                    break;
                case OPENGAT_TASKMGR_RSS:
                    memory(scratch, rows[at].rss_kib, sizeof(scratch));
                    draw_cell(surface, list, cell, scratch, baseline,
                              ink, true);
                    break;
                default:
                    (void)number(scratch, rows[at].pid, sizeof(scratch));
                    draw_cell(surface, list, cell, scratch, baseline,
                              ink, true);
                    break;
                }
            }
        }
    }

    /* the End Task button */
    {
        struct opengat_rect button;

        if (!opengat_taskmgr_end_button(window, &button)) {
            return;
        }
        opengat_surface_fill(surface, client, button, OPENGAT_BG);
        for (at = 0U; at < button.width; ++at) {
            opengat_surface_plot(surface, client, button.x + at, button.y,
                               OPENGAT_LINE_LIGHT);
            opengat_surface_plot(surface, client, button.x + at,
                button.y + button.height - 1U, OPENGAT_LINE);
        }
        for (at = 0U; at < button.height; ++at) {
            opengat_surface_plot(surface, client, button.x, button.y + at,
                               OPENGAT_LINE_LIGHT);
            opengat_surface_plot(surface, client,
                button.x + button.width - 1U, button.y + at, OPENGAT_LINE);
        }
        {
            uint32_t width = opengat_font_width("End Task");

            /* DIMMED with nothing chosen: the button cannot end what
             * has not been picked, and it should not look as though it
             * could. */
            opengat_font_draw(surface, client,
                button.x + (button.width - width) / 2U,
                button.y + 15U, "End Task",
                opengat_taskmgr_has_selection() ? OPENGAT_FG : OPENGAT_LINE);
        }
    }
}

/*
 * The self test asks what a sortable list has to get right and what the
 * JavaScript beside this got wrong first time: does sorting by a column
 * actually reorder, and does asking for the same column again reverse it
 * rather than doing nothing?
 */
bool opengat_taskmgr_self_test(void)
{
    struct opengat_taskmgr_row row;
    uint32_t first;

    opengat_taskmgr_reset();
    chosen = OPENGAT_TASKMGR_MAX_ROWS;
    sort_column = OPENGAT_TASKMGR_PID;
    sort_descending = false;

    copy(row.command, "zsh", OPENGAT_TASKMGR_NAME_BYTES);
    copy(row.user, "user", OPENGAT_TASKMGR_NAME_BYTES);
    row.cpu_tenths = 10U;
    row.rss_kib = 900U;
    row.pid = 7U;
    if (!opengat_taskmgr_add(&row)) {
        return false;
    }
    opengat_taskmgr_select(0U);
    copy(row.command, "awk", OPENGAT_TASKMGR_NAME_BYTES);
    row.cpu_tenths = 50U;
    row.rss_kib = 4096U;
    row.pid = 2U;
    if (!opengat_taskmgr_add(&row)) {
        return false;
    }
    /* Added out of order, sorted by pid: the low pid comes first. */
    if (rows[0].pid != 2U || opengat_taskmgr_selected_pid() != 7U) {
        return false;
    }
    {
        struct opengat_window window = { 0 };
        struct opengat_rect bounds;

        window.frame.width = 320U;
        window.frame.height = OPENGAT_TITLE_HEIGHT + OPENGAT_BORDER * 2U +
            TASKMGR_MENUBAR + TASKMGR_SUMMARY + TASKMGR_HEADER +
            TASKMGR_FOOTER + TASKMGR_ROW;
        if (opengat_taskmgr_row_bounds(NULL, 0U, &bounds) ||
                !opengat_taskmgr_row_bounds(&window, 0U, &bounds) ||
                opengat_taskmgr_row_bounds(&window, 1U, &bounds)) {
            return false;
        }
    }
    opengat_taskmgr_sort(OPENGAT_TASKMGR_COMMAND);
    if (compare_text(rows[0].command, "awk") != 0 ||
            opengat_taskmgr_selected_pid() != 7U) {
        return false;
    }
    opengat_taskmgr_sort(OPENGAT_TASKMGR_CPU);
    first = rows[0].cpu_tenths;
    if (first != 10U || opengat_taskmgr_selected_pid() != 7U) {
        return false;
    }
    /* The same column again REVERSES it. */
    opengat_taskmgr_sort(OPENGAT_TASKMGR_CPU);
    if (rows[0].cpu_tenths == first) {
        return false;
    }
    if (!opengat_taskmgr_sort_descending()) {
        return false;
    }
    if (opengat_taskmgr_selected_pid() != 7U) {
        return false;
    }
    chosen = OPENGAT_TASKMGR_MAX_ROWS;

    /* End Task. */
    {
        uint32_t was = opengat_taskmgr_count();

        /* Nothing chosen: the button cannot act. */
        if (opengat_taskmgr_has_selection()) {
            return false;
        }
        if (opengat_taskmgr_end_selected()) {
            return false;
        }
        opengat_taskmgr_select(0U);
        if (!opengat_taskmgr_has_selection()) {
            return false;
        }
        if (!opengat_taskmgr_end_selected()) {
            return false;
        }
        if (opengat_taskmgr_count() != was - 1U) {
            return false;
        }
        /* The selection did NOT slide onto the row that moved up. */
        if (opengat_taskmgr_has_selection()) {
            return false;
        }
    }
    /* And the session refuses to be ended. */
    {
        struct opengat_taskmgr_row session;
        uint32_t at;

        opengat_taskmgr_reset();
        copy(session.command, "opengat-session", OPENGAT_TASKMGR_NAME_BYTES);
        copy(session.user, "user", OPENGAT_TASKMGR_NAME_BYTES);
        session.cpu_tenths = 20U;
        session.rss_kib = 2400U;
        session.pid = 1U;
        if (!opengat_taskmgr_add(&session)) {
            return false;
        }
        for (at = 0U; at < opengat_taskmgr_count(); ++at) {
            if (rows[at].pid == 1U) {
                opengat_taskmgr_select(at);
            }
        }
        if (opengat_taskmgr_end_selected()) {
            return false;
        }
        if (opengat_taskmgr_count() != 1U) {
            return false;
        }
    }
    opengat_taskmgr_reset();
    return true;
}
