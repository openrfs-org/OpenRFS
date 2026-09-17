/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ORFS_LINE_H
#define ORFS_LINE_H

#include <stdbool.h>
#include <stdint.h>

#include <orfs/term.h>

/*
 * The line editor.
 *
 * ksh emacs mode, which is what OpenBSD drops you into, and which is
 * older than readline and smaller than all of it. The keys are the ones
 * ksh binds by default:
 *
 *   ^A  start of line        ^E  end of line
 *   ^B  back one             ^F  forward one
 *   ^D  delete under cursor, or end-of-file on an empty line
 *   ^H  backspace            ^K  kill to end of line
 *   ^U  kill the whole line  ^W  kill the word behind the cursor
 *   ^P  previous history     ^N  next history
 *   ^L  redraw the screen    ^C  abandon this line
 *
 * No completion. Tab inserts a tab, so what you type is what runs.
 */

#define ORFS_LINE_MAX 256U
#define ORFS_HISTORY 16U

enum orfs_line_result {
    ORFS_LINE_EDITING = 0,   /* still typing */
    ORFS_LINE_DONE,          /* Enter: the buffer is a command */
    ORFS_LINE_CANCEL,        /* ^C: abandon it */
    ORFS_LINE_EOF            /* ^D on an empty line */
};

struct orfs_line {
    char buf[ORFS_LINE_MAX];
    uint32_t len;
    uint32_t cursor;
    char history[ORFS_HISTORY][ORFS_LINE_MAX];
    uint32_t hist_count;
    uint32_t hist_at;            /* == hist_count means "the live line" */
    char saved[ORFS_LINE_MAX];   /* the live line, parked during recall */
    uint32_t prompt_len;
};

void orfs_line_reset(struct orfs_line *l);
void orfs_line_begin(struct orfs_line *l, uint32_t prompt_len);
/* One keystroke in. Redraws through the terminal so the screen and the
 * buffer can never disagree. */
enum orfs_line_result orfs_line_key(struct orfs_line *l,
                                    struct orfs_term *t, int key);
void orfs_line_remember(struct orfs_line *l, const char *s);
const char *orfs_line_text(const struct orfs_line *l);
uint32_t orfs_line_history_count(const struct orfs_line *l);
const char *orfs_line_history(const struct orfs_line *l, uint32_t at);
uint32_t orfs_line_cursor(const struct orfs_line *l);

/* Keys above 0xFF, so they cannot collide with a character. */
#define ORFS_KEY_UP     0x100
#define ORFS_KEY_DOWN   0x101
#define ORFS_KEY_LEFT   0x102
#define ORFS_KEY_RIGHT  0x103
#define ORFS_KEY_HOME   0x104
#define ORFS_KEY_END    0x105
#define ORFS_KEY_DEL    0x106
#define ORFS_KEY_PGUP   0x107
#define ORFS_KEY_PGDN   0x108
#define ORFS_KEY_F1     0x109

#endif /* ORFS_LINE_H */
