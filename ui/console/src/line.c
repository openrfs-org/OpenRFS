/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/line.h>

void rsd_line_reset(struct rsd_line *l)
{
    if (l == NULL) {
        return;
    }
    l->buf[0] = '\0';
    l->len = 0U;
    l->cursor = 0U;
    l->hist_count = 0U;
    l->hist_at = 0U;
    l->saved[0] = '\0';
    l->prompt_len = 0U;
}

void rsd_line_begin(struct rsd_line *l, uint32_t prompt_len)
{
    if (l == NULL) {
        return;
    }
    l->buf[0] = '\0';
    l->len = 0U;
    l->cursor = 0U;
    l->hist_at = l->hist_count;
    l->prompt_len = prompt_len;
}

const char *rsd_line_text(const struct rsd_line *l)
{
    return l != NULL ? l->buf : "";
}

uint32_t rsd_line_cursor(const struct rsd_line *l)
{
    return l != NULL ? l->cursor : 0U;
}

uint32_t rsd_line_history_count(const struct rsd_line *l)
{
    return l != NULL ? l->hist_count : 0U;
}

const char *rsd_line_history(const struct rsd_line *l, uint32_t at)
{
    if (l == NULL || at >= l->hist_count) {
        return "";
    }
    return l->history[at];
}

/*
 * History keeps the last RSD_HISTORY lines and drops the oldest. A blank
 * line is not a command and is not remembered; neither is a line
 * identical to the one before it, because holding Enter should not fill
 * the history with one command.
 */
void rsd_line_remember(struct rsd_line *l, const char *s)
{
    uint32_t at;

    if (l == NULL || s == NULL || s[0] == '\0') {
        return;
    }
    if (l->hist_count != 0U &&
            rsd_streq(l->history[l->hist_count - 1U], s)) {
        return;
    }
    if (l->hist_count == RSD_HISTORY) {
        for (at = 1U; at < RSD_HISTORY; ++at) {
            (void)rsd_strcopy(l->history[at - 1U], RSD_LINE_MAX,
                               l->history[at]);
        }
        --l->hist_count;
    }
    (void)rsd_strcopy(l->history[l->hist_count], RSD_LINE_MAX, s);
    ++l->hist_count;
    l->hist_at = l->hist_count;
}

/*
 * REDRAW IS THE WHOLE TRICK.
 *
 * The buffer is the truth and the screen is a picture of it. Every edit
 * wipes the line back to the prompt and paints the buffer again, then
 * walks the cursor back to where it belongs. Trying to patch the screen
 * in place - erase one character here, insert one there - is how a line
 * editor ends up showing something the buffer does not contain, and once
 * those two disagree there is no way to tell which one is lying.
 */
static void redraw(struct rsd_line *l, struct rsd_term *t)
{
    uint32_t at;

    if (t->col > l->prompt_len) {
        rsd_term_erase(t, t->col - l->prompt_len);
    }
    for (at = 0U; at < l->len; ++at) {
        rsd_term_putc(t, l->buf[at]);
    }
    /* The cursor sits where the next character goes, so walk it back
     * over anything drawn to the right of it. */
    if (l->cursor < l->len) {
        uint32_t back = l->len - l->cursor;

        while (back-- != 0U && t->col != 0U) {
            --t->col;
        }
    }
}

static void insert(struct rsd_line *l, char c)
{
    uint32_t at;

    if (l->len + 1U >= RSD_LINE_MAX) {
        return;
    }
    for (at = l->len; at > l->cursor; --at) {
        l->buf[at] = l->buf[at - 1U];
    }
    l->buf[l->cursor] = c;
    ++l->len;
    ++l->cursor;
    l->buf[l->len] = '\0';
}

static void delete_at(struct rsd_line *l, uint32_t at)
{
    if (at >= l->len) {
        return;
    }
    for (; at + 1U < l->len; ++at) {
        l->buf[at] = l->buf[at + 1U];
    }
    --l->len;
    l->buf[l->len] = '\0';
}

static void recall(struct rsd_line *l, struct rsd_term *t, bool back)
{
    if (back) {
        if (l->hist_at == 0U) {
            return;
        }
        if (l->hist_at == l->hist_count) {
            (void)rsd_strcopy(l->saved, RSD_LINE_MAX, l->buf);
        }
        --l->hist_at;
        l->len = rsd_strcopy(l->buf, RSD_LINE_MAX,
                              l->history[l->hist_at]);
    } else {
        if (l->hist_at >= l->hist_count) {
            return;
        }
        ++l->hist_at;
        if (l->hist_at == l->hist_count) {
            l->len = rsd_strcopy(l->buf, RSD_LINE_MAX, l->saved);
        } else {
            l->len = rsd_strcopy(l->buf, RSD_LINE_MAX,
                                  l->history[l->hist_at]);
        }
    }
    l->cursor = l->len;
    redraw(l, t);
}

enum rsd_line_result rsd_line_key(struct rsd_line *l,
                                    struct rsd_term *t, int key)
{
    if (l == NULL || t == NULL) {
        return RSD_LINE_EDITING;
    }

    switch (key) {
    case '\r':
    case '\n':
        rsd_term_newline(t);
        return RSD_LINE_DONE;

    case 0x03:                                   /* ^C */
        rsd_term_puts(t, "^C");
        rsd_term_newline(t);
        return RSD_LINE_CANCEL;

    case 0x04:                                   /* ^D */
        if (l->len == 0U) {
            return RSD_LINE_EOF;
        }
        delete_at(l, l->cursor);
        redraw(l, t);
        return RSD_LINE_EDITING;

    case 0x01:                                   /* ^A */
    case RSD_KEY_HOME:
        while (l->cursor != 0U) {
            --l->cursor;
            --t->col;
        }
        return RSD_LINE_EDITING;

    case 0x05:                                   /* ^E */
    case RSD_KEY_END:
        while (l->cursor < l->len) {
            ++l->cursor;
            ++t->col;
        }
        return RSD_LINE_EDITING;

    case 0x02:                                   /* ^B */
    case RSD_KEY_LEFT:
        if (l->cursor != 0U) {
            --l->cursor;
            --t->col;
        }
        return RSD_LINE_EDITING;

    case 0x06:                                   /* ^F */
    case RSD_KEY_RIGHT:
        if (l->cursor < l->len) {
            ++l->cursor;
            ++t->col;
        }
        return RSD_LINE_EDITING;

    case 0x08:                                   /* ^H */
    case 0x7F:                                   /* DEL */
        if (l->cursor != 0U) {
            --l->cursor;
            delete_at(l, l->cursor);
            redraw(l, t);
        }
        return RSD_LINE_EDITING;

    case RSD_KEY_DEL:
        delete_at(l, l->cursor);
        redraw(l, t);
        return RSD_LINE_EDITING;

    case 0x0B:                                   /* ^K kill to end */
        l->len = l->cursor;
        l->buf[l->len] = '\0';
        redraw(l, t);
        return RSD_LINE_EDITING;

    case 0x15:                                   /* ^U kill the line */
        l->len = 0U;
        l->cursor = 0U;
        l->buf[0] = '\0';
        redraw(l, t);
        return RSD_LINE_EDITING;

    case 0x17:                                   /* ^W kill a word back */
        while (l->cursor != 0U && l->buf[l->cursor - 1U] == ' ') {
            --l->cursor;
            delete_at(l, l->cursor);
        }
        while (l->cursor != 0U && l->buf[l->cursor - 1U] != ' ') {
            --l->cursor;
            delete_at(l, l->cursor);
        }
        redraw(l, t);
        return RSD_LINE_EDITING;

    case 0x10:                                   /* ^P */
    case RSD_KEY_UP:
        recall(l, t, true);
        return RSD_LINE_EDITING;

    case 0x0E:                                   /* ^N */
    case RSD_KEY_DOWN:
        recall(l, t, false);
        return RSD_LINE_EDITING;

    default:
        break;
    }

    if (key >= 0x20 && key <= 0x7E) {
        insert(l, (char)key);
        redraw(l, t);
    }
    return RSD_LINE_EDITING;
}
