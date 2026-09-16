/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_DE_TERMINAL_H
#define OPENGAT_DE_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>

#include <opengat/de/surface.h>
#include <opengat/de/window.h>

/*
 * lxterminal, the OpenGAT terminal.
 *
 * Black ground, light grey text, a monospace face and a block cursor -
 * the old look rather than the modern one, which means no transparency,
 * no rounded anything and no colour beyond what the shell itself emits.
 *
 * IT ANSWERS WHAT IS TYPED AT IT.  A terminal that prints a prompt and
 * ignores the keyboard is a picture of a terminal, so there is a command
 * table here and an unknown command says so the way a shell does.
 */

#define OPENGAT_TERM_COLUMNS 80U
#define OPENGAT_TERM_ROWS 24U
/*
 * MORE HISTORY THAN FITS.  A terminal that keeps exactly what is on
 * screen has no scrollback at all: the line that told you what went
 * wrong is gone the moment anything else prints.
 */
#define OPENGAT_TERM_HISTORY 120U
#define OPENGAT_TERM_LINE_BYTES 96U

void opengat_terminal_reset(void);

/*
 * THE LINE BEING TYPED.  A terminal that prints a prompt and cannot be
 * typed at is a picture of a terminal, so there is a real input line
 * here: characters go on the end, backspace takes one off, and return
 * runs what is there and clears it.
 */
void opengat_terminal_type(char ch);
void opengat_terminal_backspace(void);
void opengat_terminal_enter(void);
const char *opengat_terminal_input(void);
void opengat_terminal_print(const char *line);
/* Runs a command line: echoes it after the prompt, then its output. */
void opengat_terminal_run(const char *command);
uint32_t opengat_terminal_row_count(void);
const char *opengat_terminal_row(uint32_t at);

/* How far back the view is, in lines.  Nought is the bottom, which is
 * where a terminal sits unless you have moved it. */
void opengat_terminal_scroll(int32_t lines);
uint32_t opengat_terminal_scrolled(void);

void opengat_terminal_draw(struct opengat_surface *surface,
    const struct opengat_window *window);

bool opengat_terminal_self_test(void);

#endif /* OPENGAT_DE_TERMINAL_H */
