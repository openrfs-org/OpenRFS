/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ORFS_TERM_H
#define ORFS_TERM_H

#include <stdbool.h>
#include <stddef.h>   /* NULL: freestanding gives us this one */
#include <stdint.h>

/*
 * The console is a grid of characters. Nothing here knows about pixels.
 *
 * A real console writes to a framebuffer; this one writes to cells, and
 * something else turns cells into pixels. That split is why the same
 * screen can go to a framebuffer, to a serial line, or to a PNG in the
 * test harness without any of them knowing about each other.
 *
 * 80x25 because that is what a text console is. Not a choice.
 */

#define ORFS_COLS 80U
#define ORFS_ROWS 25U

/*
 * Each cell is a character and a VGA-style attribute byte:
 * (background << 4) | foreground, sixteen colours, bit 3 of the
 * foreground being the bright bit. Bold uses that bit rather than a
 * separate flag, so the two cannot disagree.
 *
 * The shell never sets a pen, so it writes 0x07 on a screen cleared to
 * 0x07 and comes out monochrome. The installer sets one.
 */
#define ORFS_BLACK   0U
#define ORFS_BLUE    1U
#define ORFS_GREEN   2U
#define ORFS_CYAN    3U
#define ORFS_RED     4U
#define ORFS_MAGENTA 5U
#define ORFS_BROWN   6U
#define ORFS_GREY    7U
#define ORFS_DARK    8U
#define ORFS_HIBLUE  9U
#define ORFS_HIGREEN 10U
#define ORFS_HICYAN  11U
#define ORFS_HIRED   12U
#define ORFS_HIMAG   13U
#define ORFS_YELLOW  14U
#define ORFS_WHITE   15U

#define ORFS_ATTR(fg, bg) ((uint8_t)((((bg) & 0x0FU) << 4) | ((fg) & 0x0FU)))
#define ORFS_ATTR_FG(a) ((uint8_t)((a) & 0x0FU))
#define ORFS_ATTR_BG(a) ((uint8_t)(((a) >> 4) & 0x0FU))

/* The console's own two: what the machine says, against what you typed. */
#define ORFS_NORMAL ORFS_ATTR(ORFS_GREY, ORFS_BLACK)
#define ORFS_BRIGHT ORFS_ATTR(ORFS_WHITE, ORFS_BLACK)

struct orfs_term {
    char cell[ORFS_ROWS][ORFS_COLS];
    uint8_t attr[ORFS_ROWS][ORFS_COLS];
    uint8_t pen;                 /* what the next character is written in */
    uint32_t col;
    uint32_t row;
    bool cursor;
};

void orfs_term_reset(struct orfs_term *t);
void orfs_term_putc(struct orfs_term *t, char c);
void orfs_term_puts(struct orfs_term *t, const char *s);
/* Bold is for one thing: what the machine said, against what you typed. */
void orfs_term_puts_bold(struct orfs_term *t, const char *s);
void orfs_term_newline(struct orfs_term *t);
void orfs_term_clear(struct orfs_term *t);
/* Backspace that stops at the start of the line rather than wrapping up
 * into the previous one - the line editor owns what may be erased. */
void orfs_term_erase(struct orfs_term *t, uint32_t count);
char orfs_term_at(const struct orfs_term *t, uint32_t row, uint32_t col);
uint8_t orfs_term_attr_at(const struct orfs_term *t, uint32_t row,
                          uint32_t col);

/*
 * Drawing by position rather than by stream.
 *
 * A shell writes forwards and the screen scrolls under it. A dialog
 * does not: it knows where its frame goes and puts it there. Both are
 * the same grid, so both live here.
 */
void orfs_term_pen(struct orfs_term *t, uint8_t attr);
void orfs_term_fill(struct orfs_term *t, char c, uint8_t attr);
void orfs_term_put_at(struct orfs_term *t, uint32_t row, uint32_t col,
                      char c, uint8_t attr);
void orfs_term_puts_at(struct orfs_term *t, uint32_t row, uint32_t col,
                       const char *s, uint8_t attr);
/* A run of one character - frames and gauges are made of these. */
void orfs_term_repeat_at(struct orfs_term *t, uint32_t row, uint32_t col,
                         char c, uint32_t count, uint8_t attr);
void orfs_term_box_fill(struct orfs_term *t, uint32_t row, uint32_t col,
                        uint32_t rows, uint32_t cols, char c, uint8_t attr);

/* The other direction: digits to a number. Anything that is not a digit
 * ends it, and an empty string is zero - a form field is read with this
 * and a form field can always be empty. */
uint32_t orfs_atou(const char *s);

/* Unsigned decimal, and hex, because there is no printf here. */
uint32_t orfs_u32(char *out, uint32_t capacity, uint32_t value);
uint32_t orfs_u32_pad(char *out, uint32_t capacity, uint32_t value,
                      uint32_t width, char pad);
uint32_t orfs_strlen(const char *s);
bool orfs_streq(const char *a, const char *b);
bool orfs_strneq(const char *a, const char *b, uint32_t n);
uint32_t orfs_strcopy(char *out, uint32_t capacity, const char *s);
/* Appends, and returns the new length. Truncates rather than running on. */
uint32_t orfs_strcat(char *out, uint32_t capacity, const char *s);

#endif /* ORFS_TERM_H */
