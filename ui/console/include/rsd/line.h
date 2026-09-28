/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_LINE_H
#define RSD_LINE_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd/term.h>

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

#define RSD_LINE_MAX 256U
#define RSD_HISTORY 16U

enum rsd_line_result {
    RSD_LINE_EDITING = 0,   /* still typing */
    RSD_LINE_DONE,          /* Enter: the buffer is a command */
    RSD_LINE_CANCEL,        /* ^C: abandon it */
    RSD_LINE_EOF            /* ^D on an empty line */
};

struct rsd_line {
    char buf[RSD_LINE_MAX];
    uint32_t len;
    uint32_t cursor;
    char history[RSD_HISTORY][RSD_LINE_MAX];
    uint32_t hist_count;
    uint32_t hist_at;            /* == hist_count means "the live line" */
    char saved[RSD_LINE_MAX];   /* the live line, parked during recall */
    uint32_t prompt_len;
};

void rsd_line_reset(struct rsd_line *l);
void rsd_line_begin(struct rsd_line *l, uint32_t prompt_len);
/* One keystroke in. Redraws through the terminal so the screen and the
 * buffer can never disagree. */
enum rsd_line_result rsd_line_key(struct rsd_line *l,
                                    struct rsd_term *t, int key);
void rsd_line_remember(struct rsd_line *l, const char *s);
const char *rsd_line_text(const struct rsd_line *l);
uint32_t rsd_line_history_count(const struct rsd_line *l);
const char *rsd_line_history(const struct rsd_line *l, uint32_t at);
uint32_t rsd_line_cursor(const struct rsd_line *l);

/* Keys above 0xFF, so they cannot collide with a character. */
#define RSD_KEY_UP     0x100
#define RSD_KEY_DOWN   0x101
#define RSD_KEY_LEFT   0x102
#define RSD_KEY_RIGHT  0x103
#define RSD_KEY_HOME   0x104
#define RSD_KEY_END    0x105
#define RSD_KEY_DEL    0x106
#define RSD_KEY_PGUP   0x107
#define RSD_KEY_PGDN   0x108
#define RSD_KEY_F1     0x109

#endif /* RSD_LINE_H */
