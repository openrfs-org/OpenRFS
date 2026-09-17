/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ORFS_INSTALL_H
#define ORFS_INSTALL_H

#include <stdbool.h>
#include <stdint.h>

#include <orfs/term.h>
#include <orfs/ui.h>

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

enum orfs_step {
    ORFS_STEP_KEYMAP = 0,   /* which keyboard, before anything is typed */
    ORFS_STEP_WELCOME,      /* install, or drop to a shell, or look round */
    ORFS_STEP_HOSTNAME,
    ORFS_STEP_COMPONENTS,
    ORFS_STEP_METHOD,       /* who lays the disk out, and how */
    ORFS_STEP_DISK,
    ORFS_STEP_SWAP,         /* only if you said you would do it yourself */
    ORFS_STEP_SCHEME,
    ORFS_STEP_REVIEW,       /* what the disk will look like */
    ORFS_STEP_COMMIT,       /* the last place to stop */
    ORFS_STEP_WRITE,
    ORFS_STEP_VERIFY,
    ORFS_STEP_ROOTPW,
    ORFS_STEP_NETIF,
    ORFS_STEP_DHCP,
    ORFS_STEP_STATIC,
    ORFS_STEP_DNS,
    ORFS_STEP_REGION,
    ORFS_STEP_ZONE,
    ORFS_STEP_STARTUP,      /* what happens at boot */
    ORFS_STEP_HARDENING,    /* what the kernel refuses to do */
    ORFS_STEP_ADDUSER,
    ORFS_STEP_FINAL,        /* anything you want to go back and change */
    ORFS_STEP_DONE,
    ORFS_STEP_SHELL,        /* handed over to the console */
    ORFS_STEP_REBOOT,
    ORFS_STEP_ABANDONED,    /* escaped out of the front door */
    ORFS_STEP_HELP          /* F1, on top of wherever you were */
};

#define ORFS_INSTALL_STEPS 28U
#define ORFS_COMPONENTS 6U
#define ORFS_STARTUP 5U
#define ORFS_HARDENING 5U

struct orfs_install {
    struct orfs_term term;
    struct orfs_ui ui;
    enum orfs_step step;

    char keymap[24];
    char hostname[32];
    char disk[12];
    uint32_t disk_mib;
    char scheme[8];
    char rootpw[ORFS_UI_VALUE];
    char user[ORFS_UI_VALUE];
    char userpw[ORFS_UI_VALUE];
    char ip[ORFS_UI_VALUE];
    char mask[ORFS_UI_VALUE];
    char router[ORFS_UI_VALUE];
    char dns1[ORFS_UI_VALUE];
    char dns2[ORFS_UI_VALUE];
    char iface[12];
    char region[16];
    char zone[24];
    bool dhcp;

    /* What was ticked. Kept as flags beside the tables that name them,
     * so a screen revisited from the final menu comes back the way it
     * was left rather than the way it started. */
    bool comp_on[ORFS_COMPONENTS];
    bool startup_on[ORFS_STARTUP];
    bool harden_on[ORFS_HARDENING];

    uint32_t swap_mib;       /* how much of the disk is swap */
    bool by_hand;            /* the layout was chosen rather than given */
    enum orfs_step help_from;  /* where F1 was pressed, to go back to */
    bool written;            /* the disk step ran to the end */
    bool verified;           /* the signature step ran to the end */
};

void orfs_install_begin(struct orfs_install *in);
void orfs_install_key(struct orfs_install *in, int key);
void orfs_install_type(struct orfs_install *in, const char *text);
/* A gauge only moves when work happens. Nothing else reads this. */
void orfs_install_tick(struct orfs_install *in);
/* Ticks until the gauge finishes and the installer has moved on. */
void orfs_install_settle(struct orfs_install *in);

const char *orfs_install_step_name(const struct orfs_install *in);
bool orfs_install_selected(const struct orfs_install *in,
                           enum orfs_step step, const char *tag);

/* One screen row as a NUL-terminated string, trailing blanks trimmed. */
void orfs_install_row(const struct orfs_install *in, uint32_t row,
                      char *out, uint32_t capacity);

#endif /* ORFS_INSTALL_H */
