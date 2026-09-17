/* SPDX-License-Identifier: GPL-3.0-only */
#include <openrfs/de/taskmgr.h>

#include <openrfs/de/font.h>
#include <openrfs/de/theme.h>

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
static const uint32_t COLUMN_WIDTH[OPENRFS_TASKMGR_COLUMNS] = {
    0U, 70U, 60U, 76U, 50U
};

static const char *const COLUMN_NAME[OPENRFS_TASKMGR_COLUMNS] = {
    "Command", "User", "CPU%", "RSS", "PID"
};

/* ================================================================== STATE */

static struct openrfs_taskmgr_row rows[OPENRFS_TASKMGR_MAX_ROWS];
static uint32_t row_count;
static enum openrfs_taskmgr_column sort_column = OPENRFS_TASKMGR_PID;
static bool sort_descending;
static uint32_t chosen = OPENRFS_TASKMGR_MAX_ROWS;

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

static int compare(const struct openrfs_taskmgr_row *a,
    const struct openrfs_taskmgr_row *b)
{
    switch (sort_column) {
    case OPENRFS_TASKMGR_COMMAND:
        return compare_text(a->command, b->command);
    case OPENRFS_TASKMGR_USER:
        return compare_text(a->user, b->user);
    case OPENRFS_TASKMGR_CPU:
        if (a->cpu_tenths == b->cpu_tenths) {
            return 0;
        }
        return a->cpu_tenths < b->cpu_tenths ? -1 : 1;
    case OPENRFS_TASKMGR_RSS:
        if (a->rss_kib == b->rss_kib) {
            return 0;
        }
        return a->rss_kib < b->rss_kib ? -1 : 1;
    case OPENRFS_TASKMGR_PID:
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
        struct openrfs_taskmgr_row held = rows[at];
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
    chosen = OPENRFS_TASKMGR_MAX_ROWS;
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

void openrfs_taskmgr_reset(void)
{
    row_count = 0U;
    chosen = OPENRFS_TASKMGR_MAX_ROWS;
}

bool openrfs_taskmgr_add(const struct openrfs_taskmgr_row *row)
{
    if (row == NULL || row_count >= OPENRFS_TASKMGR_MAX_ROWS) {
        return false;
    }
    rows[row_count] = *row;
    copy(rows[row_count].command, row->command,
         OPENRFS_TASKMGR_NAME_BYTES);
    copy(rows[row_count].user, row->user, OPENRFS_TASKMGR_NAME_BYTES);
    ++row_count;
    resort();
    return true;
}

uint32_t openrfs_taskmgr_count(void)
{
    return row_count;
}

void openrfs_taskmgr_select(uint32_t at)
{
    if (at < row_count) {
        chosen = at;
    }
}

uint32_t openrfs_taskmgr_selected(void)
{
    return chosen;
}

bool openrfs_taskmgr_has_selection(void)
{
    return chosen < row_count;
}

uint32_t openrfs_taskmgr_selected_pid(void)
{
    return chosen < row_count ? rows[chosen].pid : 0U;
}

bool openrfs_taskmgr_end_selected(void)
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
    chosen = OPENRFS_TASKMGR_MAX_ROWS;
    return true;
}

bool openrfs_taskmgr_row_bounds(const struct openrfs_window *window,
    uint32_t at, struct openrfs_rect *out)
{
    struct openrfs_rect client;
    const uint32_t list_offset = TASKMGR_MENUBAR + TASKMGR_SUMMARY +
        TASKMGR_HEADER;
    uint32_t list_height;

    if (window == NULL || out == NULL || at >= row_count) {
        return false;
    }
    client = openrfs_window_client(window);
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

bool openrfs_taskmgr_end_button(const struct openrfs_window *window,
    struct openrfs_rect *out)
{
    struct openrfs_rect client;

    if (window == NULL || out == NULL) {
        return false;
    }
    client = openrfs_window_client(window);
    out->width = 82U;
    out->height = 22U;
    out->x = client.x + client.width - out->width - TASKMGR_PAD;
    out->y = client.y + client.height - out->height - 5U;
    return true;
}

void openrfs_taskmgr_sort(enum openrfs_taskmgr_column column)
{
    if (column == sort_column) {
        sort_descending = !sort_descending;
    } else {
        sort_column = column;
        sort_descending = false;
    }
    resort();
}

enum openrfs_taskmgr_column openrfs_taskmgr_sort_column(void)
{
    return sort_column;
}

bool openrfs_taskmgr_sort_descending(void)
{
    return sort_descending;
}

/* ================================================================ LAYOUT */

static uint32_t command_width(struct openrfs_rect client)
{
    uint32_t fixed = 0U;
    uint32_t at;

    for (at = 1U; at < OPENRFS_TASKMGR_COLUMNS; ++at) {
        fixed += COLUMN_WIDTH[at];
    }
    if (client.width < fixed + TASKMGR_PAD * 2U + 60U) {
        return 60U;
    }
    return client.width - fixed - TASKMGR_PAD * 2U;
}

bool openrfs_taskmgr_header_bounds(const struct openrfs_window *window,
    enum openrfs_taskmgr_column column, struct openrfs_rect *out)
{
    struct openrfs_rect client;
    uint32_t left;
    uint32_t at;

    if (window == NULL || out == NULL ||
            (uint32_t)column >= OPENRFS_TASKMGR_COLUMNS) {
        return false;
    }
    client = openrfs_window_client(window);
    left = client.x + TASKMGR_PAD;
    for (at = 0U; at < (uint32_t)column; ++at) {
        left += at == 0U ? command_width(client) : COLUMN_WIDTH[at];
    }
    out->x = left;
    out->y = client.y + TASKMGR_MENUBAR + TASKMGR_SUMMARY;
    out->width = column == OPENRFS_TASKMGR_COMMAND ?
        command_width(client) : COLUMN_WIDTH[column];
    out->height = TASKMGR_HEADER;
    return true;
}

/* ================================================================ DRAWING */

static void draw_cell(struct openrfs_surface *surface, struct openrfs_rect clip,
    struct openrfs_rect box, const char *text, uint32_t baseline,
    uint32_t ink, bool right_aligned)
{
    uint32_t width = openrfs_font_width(text);
    uint32_t x = box.x + 4U;

    if (right_aligned && box.width > width + 8U) {
        x = box.x + box.width - width - 6U;
    }
    openrfs_font_draw(surface, clip, x, baseline, text, ink);
}

void openrfs_taskmgr_draw(struct openrfs_surface *surface,
    const struct openrfs_window *window)
{
    struct openrfs_rect client;
    struct openrfs_rect strip;
    char scratch[24];
    uint32_t at;
    uint32_t column;
    uint32_t list_top;

    if (window == NULL || !openrfs_surface_valid(surface)) {
        return;
    }
    client = openrfs_window_client(window);
    openrfs_surface_fill(surface, client, client, OPENRFS_BG);

    /* the menu bar */
    strip = client;
    strip.height = TASKMGR_MENUBAR;
    openrfs_surface_fill(surface, client, strip, OPENRFS_BG);
    {
        static const char *const MENUS[3] = { "File", "View", "Help" };
        uint32_t pen = client.x + TASKMGR_PAD;

        for (at = 0U; at < 3U; ++at) {
            openrfs_font_draw(surface, client, pen,
                client.y + 14U, MENUS[at], OPENRFS_FG);
            pen += openrfs_font_width(MENUS[at]) + 14U;
        }
    }
    for (at = 0U; at < client.width; ++at) {
        openrfs_surface_plot(surface, client, client.x + at,
            client.y + TASKMGR_MENUBAR - 1U, OPENRFS_LINE);
    }

    /* the summary line */
    {
        uint32_t pen = client.x + TASKMGR_PAD;
        uint32_t base = client.y + TASKMGR_MENUBAR + 14U;

        openrfs_font_draw(surface, client, pen, base, "Tasks:", OPENRFS_TEXT);
        pen += openrfs_font_width("Tasks:") + 5U;
        (void)number(scratch, row_count, sizeof(scratch));
        openrfs_font_draw(surface, client, pen, base, scratch, OPENRFS_TEXT);
    }

    /* the column header, with the sorted one marked */
    for (column = 0U; column < OPENRFS_TASKMGR_COLUMNS; ++column) {
        struct openrfs_rect head;

        if (!openrfs_taskmgr_header_bounds(window,
                (enum openrfs_taskmgr_column)column, &head)) {
            continue;
        }
        openrfs_surface_fill(surface, client, head, OPENRFS_BG_ACTIVE);
        for (at = 0U; at < head.height; ++at) {
            openrfs_surface_plot(surface, client,
                head.x + head.width - 1U, head.y + at, OPENRFS_LINE);
        }
        for (at = 0U; at < head.width; ++at) {
            openrfs_surface_plot(surface, client, head.x + at,
                head.y + head.height - 1U, OPENRFS_LINE);
        }
        openrfs_font_draw(surface, head, head.x + 4U, head.y + 13U,
            COLUMN_NAME[column], OPENRFS_FG);
        if ((enum openrfs_taskmgr_column)column == sort_column &&
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

                    openrfs_surface_plot(surface, head, tip + span, row,
                                       OPENRFS_FG);
                    openrfs_surface_plot(surface, head, tip - span, row,
                                       OPENRFS_FG);
                }
            }
        }
    }

    /* the rows */
    list_top = client.y + TASKMGR_MENUBAR + TASKMGR_SUMMARY +
        TASKMGR_HEADER;
    {
        struct openrfs_rect list;

        list.x = client.x;
        list.y = list_top;
        list.width = client.width;
        list.height = client.height > (list_top - client.y) +
            TASKMGR_FOOTER ?
            client.height - (list_top - client.y) - TASKMGR_FOOTER : 0U;
        openrfs_surface_fill(surface, client, list, OPENRFS_BASE);

        for (at = 0U; at < row_count; ++at) {
            uint32_t top = list_top + at * TASKMGR_ROW;
            uint32_t baseline = top + 12U;
            uint32_t ink = at == chosen ? OPENRFS_SEL_FG : OPENRFS_TEXT;

            if (top + TASKMGR_ROW > list.y + list.height) {
                break;
            }
            if (at == chosen) {
                struct openrfs_rect band;

                band.x = list.x;
                band.y = top;
                band.width = list.width;
                band.height = TASKMGR_ROW;
                openrfs_surface_fill(surface, list, band, OPENRFS_SEL_BG);
            } else if ((at & 1U) != 0U) {
                struct openrfs_rect band;

                band.x = list.x;
                band.y = top;
                band.width = list.width;
                band.height = TASKMGR_ROW;
                openrfs_surface_fill(surface, list, band,
                                   OPENRFS_BASE_PRELIGHT);
            }
            for (column = 0U; column < OPENRFS_TASKMGR_COLUMNS; ++column) {
                struct openrfs_rect head;
                struct openrfs_rect cell;

                if (!openrfs_taskmgr_header_bounds(window,
                        (enum openrfs_taskmgr_column)column, &head)) {
                    continue;
                }
                cell = head;
                cell.y = top;
                cell.height = TASKMGR_ROW;
                switch (column) {
                case OPENRFS_TASKMGR_COMMAND:
                    draw_cell(surface, list, cell, rows[at].command,
                              baseline, ink, false);
                    break;
                case OPENRFS_TASKMGR_USER:
                    draw_cell(surface, list, cell, rows[at].user,
                              baseline, ink, false);
                    break;
                case OPENRFS_TASKMGR_CPU:
                    tenths(scratch, rows[at].cpu_tenths, sizeof(scratch));
                    draw_cell(surface, list, cell, scratch, baseline,
                              ink, true);
                    break;
                case OPENRFS_TASKMGR_RSS:
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
        struct openrfs_rect button;

        if (!openrfs_taskmgr_end_button(window, &button)) {
            return;
        }
        openrfs_surface_fill(surface, client, button, OPENRFS_BG);
        for (at = 0U; at < button.width; ++at) {
            openrfs_surface_plot(surface, client, button.x + at, button.y,
                               OPENRFS_LINE_LIGHT);
            openrfs_surface_plot(surface, client, button.x + at,
                button.y + button.height - 1U, OPENRFS_LINE);
        }
        for (at = 0U; at < button.height; ++at) {
            openrfs_surface_plot(surface, client, button.x, button.y + at,
                               OPENRFS_LINE_LIGHT);
            openrfs_surface_plot(surface, client,
                button.x + button.width - 1U, button.y + at, OPENRFS_LINE);
        }
        {
            uint32_t width = openrfs_font_width("End Task");

            /* DIMMED with nothing chosen: the button cannot end what
             * has not been picked, and it should not look as though it
             * could. */
            openrfs_font_draw(surface, client,
                button.x + (button.width - width) / 2U,
                button.y + 15U, "End Task",
                openrfs_taskmgr_has_selection() ? OPENRFS_FG : OPENRFS_LINE);
        }
    }
}

/*
 * The self test asks what a sortable list has to get right and what the
 * JavaScript beside this got wrong first time: does sorting by a column
 * actually reorder, and does asking for the same column again reverse it
 * rather than doing nothing?
 */
bool openrfs_taskmgr_self_test(void)
{
    struct openrfs_taskmgr_row row;
    uint32_t first;

    openrfs_taskmgr_reset();
    chosen = OPENRFS_TASKMGR_MAX_ROWS;
    sort_column = OPENRFS_TASKMGR_PID;
    sort_descending = false;

    copy(row.command, "zsh", OPENRFS_TASKMGR_NAME_BYTES);
    copy(row.user, "user", OPENRFS_TASKMGR_NAME_BYTES);
    row.cpu_tenths = 10U;
    row.rss_kib = 900U;
    row.pid = 7U;
    if (!openrfs_taskmgr_add(&row)) {
        return false;
    }
    openrfs_taskmgr_select(0U);
    copy(row.command, "awk", OPENRFS_TASKMGR_NAME_BYTES);
    row.cpu_tenths = 50U;
    row.rss_kib = 4096U;
    row.pid = 2U;
    if (!openrfs_taskmgr_add(&row)) {
        return false;
    }
    /* Added out of order, sorted by pid: the low pid comes first. */
    if (rows[0].pid != 2U || openrfs_taskmgr_selected_pid() != 7U) {
        return false;
    }
    {
        struct openrfs_window window = { 0 };
        struct openrfs_rect bounds;

        window.frame.width = 320U;
        window.frame.height = OPENRFS_TITLE_HEIGHT + OPENRFS_BORDER * 2U +
            TASKMGR_MENUBAR + TASKMGR_SUMMARY + TASKMGR_HEADER +
            TASKMGR_FOOTER + TASKMGR_ROW;
        if (openrfs_taskmgr_row_bounds(NULL, 0U, &bounds) ||
                !openrfs_taskmgr_row_bounds(&window, 0U, &bounds) ||
                openrfs_taskmgr_row_bounds(&window, 1U, &bounds)) {
            return false;
        }
    }
    openrfs_taskmgr_sort(OPENRFS_TASKMGR_COMMAND);
    if (compare_text(rows[0].command, "awk") != 0 ||
            openrfs_taskmgr_selected_pid() != 7U) {
        return false;
    }
    openrfs_taskmgr_sort(OPENRFS_TASKMGR_CPU);
    first = rows[0].cpu_tenths;
    if (first != 10U || openrfs_taskmgr_selected_pid() != 7U) {
        return false;
    }
    /* The same column again REVERSES it. */
    openrfs_taskmgr_sort(OPENRFS_TASKMGR_CPU);
    if (rows[0].cpu_tenths == first) {
        return false;
    }
    if (!openrfs_taskmgr_sort_descending()) {
        return false;
    }
    if (openrfs_taskmgr_selected_pid() != 7U) {
        return false;
    }
    chosen = OPENRFS_TASKMGR_MAX_ROWS;

    /* End Task. */
    {
        uint32_t was = openrfs_taskmgr_count();

        /* Nothing chosen: the button cannot act. */
        if (openrfs_taskmgr_has_selection()) {
            return false;
        }
        if (openrfs_taskmgr_end_selected()) {
            return false;
        }
        openrfs_taskmgr_select(0U);
        if (!openrfs_taskmgr_has_selection()) {
            return false;
        }
        if (!openrfs_taskmgr_end_selected()) {
            return false;
        }
        if (openrfs_taskmgr_count() != was - 1U) {
            return false;
        }
        /* The selection did NOT slide onto the row that moved up. */
        if (openrfs_taskmgr_has_selection()) {
            return false;
        }
    }
    /* And the session refuses to be ended. */
    {
        struct openrfs_taskmgr_row session;
        uint32_t at;

        openrfs_taskmgr_reset();
        copy(session.command, "openrfs-session", OPENRFS_TASKMGR_NAME_BYTES);
        copy(session.user, "user", OPENRFS_TASKMGR_NAME_BYTES);
        session.cpu_tenths = 20U;
        session.rss_kib = 2400U;
        session.pid = 1U;
        if (!openrfs_taskmgr_add(&session)) {
            return false;
        }
        for (at = 0U; at < openrfs_taskmgr_count(); ++at) {
            if (rows[at].pid == 1U) {
                openrfs_taskmgr_select(at);
            }
        }
        if (openrfs_taskmgr_end_selected()) {
            return false;
        }
        if (openrfs_taskmgr_count() != 1U) {
            return false;
        }
    }
    openrfs_taskmgr_reset();
    return true;
}
