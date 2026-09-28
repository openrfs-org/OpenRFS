/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_INSTALL_H
#define RSD_INSTALL_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd/term.h>
#include <rsd/ui.h>

/*
 * The installer.
 *
 * You boot the medium and this is what you get: no graphics, no mouse,
 * one dialog at a time on a red field. The order of the questions is
 * bsdinstall's. Keyboard, layout,
 * hostname, what to install, where to put it, what it will do to the
 * disk, and then - only then - it writes anything.
 *
 * It is a state machine, not a program with a main loop. One key goes
 * in, one screen comes out, and the step it is on is readable from the
 * outside. That is what lets the same installer run on a framebuffer,
 * over a serial line, and under a test that presses every key of a full
 * installation and looks at what ended up on the screen.
 *
 * Nothing here is written to a disk, because there is no disk here. The
 * answers are collected and the steps that would write announce exactly
 * what they would write.
 */

enum rsd_step {
    RSD_STEP_KEYMAP = 0,   /* which keyboard, before anything is typed */
    RSD_STEP_WELCOME,      /* install, or drop to a shell, or look round */
    RSD_STEP_HOSTNAME,
    RSD_STEP_COMPONENTS,
    RSD_STEP_METHOD,       /* who lays the disk out, and how */
    RSD_STEP_DISK,
    RSD_STEP_SWAP,         /* only if you said you would do it yourself */
    RSD_STEP_SCHEME,
    RSD_STEP_REVIEW,       /* what the disk will look like */
    RSD_STEP_COMMIT,       /* the last place to stop */
    RSD_STEP_WRITE,
    RSD_STEP_VERIFY,
    RSD_STEP_ROOTPW,
    RSD_STEP_NETIF,
    RSD_STEP_DHCP,
    RSD_STEP_STATIC,
    RSD_STEP_DNS,
    RSD_STEP_REGION,
    RSD_STEP_ZONE,
    RSD_STEP_STARTUP,      /* what happens at boot */
    RSD_STEP_HARDENING,    /* what the kernel refuses to do */
    RSD_STEP_ADDUSER,
    RSD_STEP_FINAL,        /* anything you want to go back and change */
    RSD_STEP_DONE,
    RSD_STEP_SHELL,        /* handed over to the console */
    RSD_STEP_REBOOT,
    RSD_STEP_ABANDONED,    /* escaped out of the front door */
    RSD_STEP_HELP          /* F1, on top of wherever you were */
};

#define RSD_INSTALL_STEPS 28U
/*
 * THE TRANSCRIPT.  OpenBSD's installer does not repaint a screen; it
 * prints a question, reads a line, and prints the next one under it, so
 * what you are looking at when you answer the tenth question is the nine
 * answers above it.  This is that scrollback, and 48 lines of it is
 * twice the screen - the terminal drops what runs off the top, which is
 * also what a console does.
 */
#define RSD_LOG_LINES 48U
#define RSD_LOG_COLS 80U
#define RSD_COMPONENTS 6U
#define RSD_STARTUP 5U
#define RSD_HARDENING 5U

struct rsd_install {
    struct rsd_term term;
    struct rsd_ui ui;
    enum rsd_step step;

    char keymap[24];
    char hostname[32];
    char disk[12];
    uint32_t disk_mib;
    char scheme[8];
    char rootpw[RSD_UI_VALUE];
    char user[RSD_UI_VALUE];
    char userpw[RSD_UI_VALUE];
    char ip[RSD_UI_VALUE];
    char mask[RSD_UI_VALUE];
    char router[RSD_UI_VALUE];
    char dns1[RSD_UI_VALUE];
    char dns2[RSD_UI_VALUE];
    char iface[12];
    char region[16];
    char zone[24];
    bool dhcp;

    /* What was ticked. Kept as flags beside the tables that name them,
     * so a screen revisited from the final menu comes back the way it
     * was left rather than the way it started. */
    bool comp_on[RSD_COMPONENTS];
    bool startup_on[RSD_STARTUP];
    bool harden_on[RSD_HARDENING];

    uint32_t swap_mib;       /* how much of the disk is swap */
    bool by_hand;            /* the layout was chosen rather than given */
    char log[RSD_LOG_LINES][RSD_LOG_COLS];
    uint32_t logged;

    enum rsd_step help_from;  /* where F1 was pressed, to go back to */
    bool written;            /* the disk step ran to the end */
    bool verified;           /* the signature step ran to the end */
};

void rsd_install_begin(struct rsd_install *in);
void rsd_install_key(struct rsd_install *in, int key);
void rsd_install_type(struct rsd_install *in, const char *text);
/* A gauge only moves when work happens. Nothing else reads this. */
void rsd_install_tick(struct rsd_install *in);
/* Ticks until the gauge finishes and the installer has moved on. */
void rsd_install_settle(struct rsd_install *in);

const char *rsd_install_step_name(const struct rsd_install *in);
bool rsd_install_selected(const struct rsd_install *in,
                           enum rsd_step step, const char *tag);

/* One screen row as a NUL-terminated string, trailing blanks trimmed. */
void rsd_install_row(const struct rsd_install *in, uint32_t row,
                      char *out, uint32_t capacity);

#endif /* RSD_INSTALL_H */
