/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_SHELL_H
#define RSD_SHELL_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd/config.h>
#include <rsd/line.h>
#include <rsd/sys.h>
#include <rsd/term.h>
#include <rsd/ui.h>

/*
 * The shell, and the boot that arrives at it.
 *
 * The loader puts up a menu, copied from FreeBSD's:
 * a picture, a list of numbered choices, and a default that happens if
 * you press return. Past that it is OpenBSD's console - the copyright,
 * the kernel line, the devices it probed, the filesystem check, and
 * then "login:" with nothing in between.
 *
 * Then "rsd$", which is the prompt RSD's own documentation
 * already shows, or "#" if you came in through single user. Nothing is
 * coloured and nothing is drawn in a box. The screen is 80x25 of
 * monospaced text and that is the entire design.
 *
 * An unknown command says so and stops. There is no "did you mean", no
 * suggestion, no search of the internet. The one thing a console owes
 * you is that it does what you typed or tells you it did not.
 */

#define RSD_ARGS 8U
#define RSD_ARG_MAX 40U

struct rsd_argv {
    char arg[RSD_ARGS][RSD_ARG_MAX];
    uint32_t count;
};

/* Splits on runs of spaces, honours '...' and "..." and a backslash
 * escape, and truncates rather than overflowing. */
void rsd_parse(const char *line, struct rsd_argv *out);

enum rsd_phase {
    RSD_PHASE_LOADER = 0,   /* the menu the loader puts up first */
    RSD_PHASE_LOADER_CMD,   /* boot> , for when the menu is not enough */
    RSD_PHASE_SINGLE_ASK,   /* which shell single user should start */
    RSD_PHASE_BOOT,
    RSD_PHASE_LOGIN,
    RSD_PHASE_PASSWORD,
    RSD_PHASE_SHELL,
    RSD_PHASE_CONFIG,       /* config(8), full screen */
    RSD_PHASE_UKC,          /* config -e, one line at a time */
    RSD_PHASE_HALTED
};

struct rsd_shell {
    struct rsd_term term;
    struct rsd_line line;
    enum rsd_phase phase;
    uint32_t cwd;
    bool desktop;
    /* Single user: no login, no network, root on a read-only filesystem
     * and a # prompt. It is how you get in when the thing that is wrong
     * is the thing that would otherwise let you in. */
    bool single;
    /* config(8) borrows the installer's dialog toolkit. -1 on the list
     * of groups, otherwise the group being edited. */
    struct rsd_ui ui;
    int32_t conf_group;
    bool conf_help;     /* F1 is showing over one of the two */
    uint32_t status;             /* the last command's exit status */
};

void rsd_boot(struct rsd_shell *sh);
/* config(8): the full-screen editor, and OpenBSD's line-oriented one. */
void rsd_config_enter(struct rsd_shell *sh);
void rsd_ukc_enter(struct rsd_shell *sh);
void rsd_key(struct rsd_shell *sh, int key);
void rsd_type(struct rsd_shell *sh, const char *text);
void rsd_run(struct rsd_shell *sh, const char *command);
const char *rsd_prompt(const struct rsd_shell *sh);
uint32_t rsd_prompt_len(const struct rsd_shell *sh);

/* One screen row as a NUL-terminated string, trailing blanks trimmed.
 * The harness reads the screen through this and so do the checks. */
void rsd_screen_row(const struct rsd_shell *sh, uint32_t row, char *out,
                     uint32_t capacity);

#endif /* RSD_SHELL_H */
