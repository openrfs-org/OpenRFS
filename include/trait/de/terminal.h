/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_DE_TERMINAL_H
#define TRAIT_DE_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>

#include <trait/de/surface.h>
#include <trait/de/window.h>

/*
 * lxterminal, the Trait OS terminal.
 *
 * Black ground, light grey text, a monospace face and a block cursor -
 * the old look rather than the modern one, which means no transparency,
 * no rounded anything and no colour beyond what the shell itself emits.
 *
 * IT ANSWERS WHAT IS TYPED AT IT.  A terminal that prints a prompt and
 * ignores the keyboard is a picture of a terminal, so there is a command
 * table here and an unknown command says so the way a shell does.
 */

#define TRAIT_TERM_COLUMNS 80U
#define TRAIT_TERM_ROWS 24U
/*
 * MORE HISTORY THAN FITS.  A terminal that keeps exactly what is on
 * screen has no scrollback at all: the line that told you what went
 * wrong is gone the moment anything else prints.
 */
#define TRAIT_TERM_HISTORY 120U
#define TRAIT_TERM_LINE_BYTES 96U

void trait_terminal_reset(void);

/*
 * THE LINE BEING TYPED.  A terminal that prints a prompt and cannot be
 * typed at is a picture of a terminal, so there is a real input line
 * here: characters go on the end, backspace takes one off, and return
 * runs what is there and clears it.
 */
void trait_terminal_type(char ch);
void trait_terminal_backspace(void);
void trait_terminal_enter(void);
const char *trait_terminal_input(void);
void trait_terminal_print(const char *line);
/* Runs a command line: echoes it after the prompt, then its output. */
void trait_terminal_run(const char *command);
uint32_t trait_terminal_row_count(void);
const char *trait_terminal_row(uint32_t at);

/* How far back the view is, in lines.  Nought is the bottom, which is
 * where a terminal sits unless you have moved it. */
void trait_terminal_scroll(int32_t lines);
uint32_t trait_terminal_scrolled(void);

void trait_terminal_draw(struct trait_surface *surface,
    const struct trait_window *window);

bool trait_terminal_self_test(void);

#endif /* TRAIT_DE_TERMINAL_H */
