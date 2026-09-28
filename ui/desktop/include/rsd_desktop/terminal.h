/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_TERMINAL_H
#define RSD_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd_desktop/surface.h>
#include <rsd_desktop/window.h>

/*
 * lxterminal, which is LXDE's terminal.
 *
 * Black ground, light grey text, a monospace face and a block cursor -
 * the old look rather than the modern one, which means no transparency,
 * no rounded anything and no colour beyond what the shell itself emits.
 *
 * IT ANSWERS WHAT IS TYPED AT IT.  A terminal that prints a prompt and
 * ignores the keyboard is a picture of a terminal, so there is a command
 * table here and an unknown command says so the way a shell does.
 */

#define RSD_TERM_COLUMNS 80U
#define RSD_TERM_ROWS 24U
/*
 * MORE HISTORY THAN FITS.  A terminal that keeps exactly what is on
 * screen has no scrollback at all: the line that told you what went
 * wrong is gone the moment anything else prints.
 */
#define RSD_TERM_HISTORY 120U
#define RSD_TERM_LINE_BYTES 96U

void rsd_terminal_reset(void);

/*
 * THE LINE BEING TYPED.  A terminal that prints a prompt and cannot be
 * typed at is a picture of a terminal, so there is a real input line
 * here: characters go on the end, backspace takes one off, and return
 * runs what is there and clears it.
 */
void rsd_terminal_type(char ch);
void rsd_terminal_backspace(void);
void rsd_terminal_enter(void);
const char *rsd_terminal_input(void);
void rsd_terminal_print(const char *line);
/* Runs a command line: echoes it after the prompt, then its output. */
void rsd_terminal_run(const char *command);

/*
 * TRANSPARENCY, the way a terminal has always had it: the window is
 * drawn after what is behind it, so its ground is mixed with what is
 * already in the framebuffer rather than composited by a server that
 * is not here.  Off makes it opaque; nothing else changes.
 */
void rsd_terminal_set_opacity(uint32_t alpha);
uint32_t rsd_terminal_opacity(void);
bool rsd_terminal_transparent(void);
void rsd_terminal_set_transparent(bool sheer);
/* The foreground in use, which follows the ground: white while the
 * terminal is see-through, lxterminal's own light grey while it is not.
 * There is no compositor to hold the ink opaque as the ground clears,
 * so the contrast the transparency spends is bought back here. */
uint32_t rsd_terminal_ink(void);
uint32_t rsd_terminal_row_count(void);
const char *rsd_terminal_row(uint32_t at);

/* How far back the view is, in lines.  Nought is the bottom, which is
 * where a terminal sits unless you have moved it. */
void rsd_terminal_scroll(int32_t lines);
uint32_t rsd_terminal_scrolled(void);

/* One of Konsole's sixteen, out of its Linux.colorscheme. Exposed so a
 * check can read the table rather than trust a screenshot of it. */
uint32_t rsd_terminal_ansi(uint32_t index);

void rsd_terminal_draw(struct rsd_surface *surface,
    const struct rsd_window *window);

bool rsd_terminal_self_test(void);

#endif /* RSD_TERMINAL_H */
