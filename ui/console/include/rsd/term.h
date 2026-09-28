/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_TERM_H
#define RSD_TERM_H

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

#define RSD_COLS 80U
#define RSD_ROWS 25U

/*
 * Each cell is a character and a VGA-style attribute byte:
 * (background << 4) | foreground, sixteen colours, bit 3 of the
 * foreground being the bright bit. Bold uses that bit rather than a
 * separate flag, so the two cannot disagree.
 *
 * The shell never sets a pen, so it writes 0x07 on a screen cleared to
 * 0x07 and comes out monochrome. The installer sets one.
 */
#define RSD_BLACK   0U
#define RSD_BLUE    1U
#define RSD_GREEN   2U
#define RSD_CYAN    3U
#define RSD_RED     4U
#define RSD_MAGENTA 5U
#define RSD_BROWN   6U
#define RSD_GREY    7U
#define RSD_DARK    8U
#define RSD_HIBLUE  9U
#define RSD_HIGREEN 10U
#define RSD_HICYAN  11U
#define RSD_HIRED   12U
#define RSD_HIMAG   13U
#define RSD_YELLOW  14U
#define RSD_WHITE   15U

#define RSD_ATTR(fg, bg) ((uint8_t)((((bg) & 0x0FU) << 4) | ((fg) & 0x0FU)))
#define RSD_ATTR_FG(a) ((uint8_t)((a) & 0x0FU))
#define RSD_ATTR_BG(a) ((uint8_t)(((a) >> 4) & 0x0FU))

/* The console's own two: what the machine says, against what you typed. */
#define RSD_NORMAL RSD_ATTR(RSD_GREY, RSD_BLACK)
#define RSD_BRIGHT RSD_ATTR(RSD_WHITE, RSD_BLACK)

struct rsd_term {
    char cell[RSD_ROWS][RSD_COLS];
    uint8_t attr[RSD_ROWS][RSD_COLS];
    uint8_t pen;                 /* what the next character is written in */
    uint32_t col;
    uint32_t row;
    bool cursor;
};

void rsd_term_reset(struct rsd_term *t);
void rsd_term_putc(struct rsd_term *t, char c);
void rsd_term_puts(struct rsd_term *t, const char *s);
/* Bold is for one thing: what the machine said, against what you typed. */
void rsd_term_puts_bold(struct rsd_term *t, const char *s);
void rsd_term_newline(struct rsd_term *t);
void rsd_term_clear(struct rsd_term *t);
/* Backspace that stops at the start of the line rather than wrapping up
 * into the previous one - the line editor owns what may be erased. */
void rsd_term_erase(struct rsd_term *t, uint32_t count);
char rsd_term_at(const struct rsd_term *t, uint32_t row, uint32_t col);
uint8_t rsd_term_attr_at(const struct rsd_term *t, uint32_t row,
                          uint32_t col);

/*
 * Drawing by position rather than by stream.
 *
 * A shell writes forwards and the screen scrolls under it. A dialog
 * does not: it knows where its frame goes and puts it there. Both are
 * the same grid, so both live here.
 */
void rsd_term_pen(struct rsd_term *t, uint8_t attr);
void rsd_term_fill(struct rsd_term *t, char c, uint8_t attr);
void rsd_term_put_at(struct rsd_term *t, uint32_t row, uint32_t col,
                      char c, uint8_t attr);
void rsd_term_puts_at(struct rsd_term *t, uint32_t row, uint32_t col,
                       const char *s, uint8_t attr);
/* A run of one character - frames and gauges are made of these. */
void rsd_term_repeat_at(struct rsd_term *t, uint32_t row, uint32_t col,
                         char c, uint32_t count, uint8_t attr);
void rsd_term_box_fill(struct rsd_term *t, uint32_t row, uint32_t col,
                        uint32_t rows, uint32_t cols, char c, uint8_t attr);

/* The other direction: digits to a number. Anything that is not a digit
 * ends it, and an empty string is zero - a form field is read with this
 * and a form field can always be empty. */
uint32_t rsd_atou(const char *s);

/* Unsigned decimal, and hex, because there is no printf here. */
uint32_t rsd_u32(char *out, uint32_t capacity, uint32_t value);
uint32_t rsd_u32_pad(char *out, uint32_t capacity, uint32_t value,
                      uint32_t width, char pad);
uint32_t rsd_strlen(const char *s);
bool rsd_streq(const char *a, const char *b);
bool rsd_strneq(const char *a, const char *b, uint32_t n);
uint32_t rsd_strcopy(char *out, uint32_t capacity, const char *s);
/* Appends, and returns the new length. Truncates rather than running on. */
uint32_t rsd_strcat(char *out, uint32_t capacity, const char *s);

#endif /* RSD_TERM_H */
