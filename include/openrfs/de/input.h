/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DE_INPUT_H
#define OPENRFS_DE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <openrfs/de/surface.h>

/*
 * Pointer and keyboard events, as the shell receives them.
 *
 * A desktop that draws but does not answer is a picture of a desktop.
 * These are the events; openrfs/shell.h is what routes them.
 */

enum openrfs_event_kind {
    OPENRFS_EVENT_POINTER_DOWN = 0,
    OPENRFS_EVENT_POINTER_UP,
    OPENRFS_EVENT_POINTER_MOVE,
    OPENRFS_EVENT_KEY
};

/* Modifiers as a set, because Ctrl+Shift+click means something that
 * neither of them means alone. */
#define OPENRFS_MOD_CTRL 0x1U
#define OPENRFS_MOD_SHIFT 0x2U
#define OPENRFS_MOD_ALT 0x4U
#define OPENRFS_MOD_SUPER 0x8U

struct openrfs_event {
    enum openrfs_event_kind kind;
    uint32_t x;
    uint32_t y;
    uint32_t modifiers;
    char key;               /* printable, or 0 */
    uint32_t special;       /* OPENRFS_KEY_*, or 0 */
    bool double_click;
    /* The secondary button.  A context menu opened by the same press
     * that selects would fire every time you clicked anything. */
    bool secondary;
};

#define OPENRFS_KEY_ENTER 1U
#define OPENRFS_KEY_BACKSPACE 2U
#define OPENRFS_KEY_TAB 3U
#define OPENRFS_KEY_ESCAPE 4U
#define OPENRFS_KEY_F4 5U

#endif /* OPENRFS_DE_INPUT_H */
