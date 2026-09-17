/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DE_TERMINAL_H
#define OPENRFS_DE_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>

#include <openrfs/de/surface.h>
#include <openrfs/de/window.h>

/*
 * lxterminal, the OpenRFS terminal.
 *
 * Black ground, light grey text, a monospace face and a block cursor -
 * the old look rather than the modern one, which means no transparency,
 * no rounded anything and no colour beyond what the shell itself emits.
 *
 * IT ANSWERS WHAT IS TYPED AT IT.  A terminal that prints a prompt and
 * ignores the keyboard is a picture of a terminal, so there is a command
 * table here and an unknown command says so the way a shell does.
 */

#define OPENRFS_TERM_COLUMNS 80U
#define OPENRFS_TERM_ROWS 24U
/*
 * MORE HISTORY THAN FITS.  A terminal that keeps exactly what is on
 * screen has no scrollback at all: the line that told you what went
 * wrong is gone the moment anything else prints.
 */
#define OPENRFS_TERM_HISTORY 120U
#define OPENRFS_TERM_LINE_BYTES 96U

void openrfs_terminal_reset(void);

/*
 * THE LINE BEING TYPED.  A terminal that prints a prompt and cannot be
 * typed at is a picture of a terminal, so there is a real input line
 * here: characters go on the end, backspace takes one off, and return
 * runs what is there and clears it.
 */
void openrfs_terminal_type(char ch);
void openrfs_terminal_backspace(void);
void openrfs_terminal_enter(void);
const char *openrfs_terminal_input(void);
void openrfs_terminal_print(const char *line);
/* Runs a command line: echoes it after the prompt, then its output. */
void openrfs_terminal_run(const char *command);
uint32_t openrfs_terminal_row_count(void);
const char *openrfs_terminal_row(uint32_t at);

/* How far back the view is, in lines.  Nought is the bottom, which is
 * where a terminal sits unless you have moved it. */
void openrfs_terminal_scroll(int32_t lines);
uint32_t openrfs_terminal_scrolled(void);

void openrfs_terminal_draw(struct openrfs_surface *surface,
    const struct openrfs_window *window);

bool openrfs_terminal_self_test(void);

#endif /* OPENRFS_DE_TERMINAL_H */
