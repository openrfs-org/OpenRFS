/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_DESKTOP_INPUT_H
#define RSD_DESKTOP_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd_desktop/surface.h>

/*
 * Pointer and keyboard events, as the shell receives them.
 *
 * A desktop that draws but does not answer is a picture of a desktop.
 * These are the events; rsd/shell.h is what routes them.
 */

enum rsd_event_kind {
    RSD_EVENT_POINTER_DOWN = 0,
    RSD_EVENT_POINTER_UP,
    RSD_EVENT_POINTER_MOVE,
    RSD_EVENT_KEY
};

/* Modifiers as a set, because Ctrl+Shift+click means something that
 * neither of them means alone. */
#define RSD_MOD_CTRL 0x1U
#define RSD_MOD_SHIFT 0x2U
#define RSD_MOD_ALT 0x4U
#define RSD_MOD_SUPER 0x8U

struct rsd_event {
    enum rsd_event_kind kind;
    uint32_t x;
    uint32_t y;
    uint32_t modifiers;
    char key;               /* printable, or 0 */
    uint32_t special;       /* RSD_KEY_*, or 0 */
    bool double_click;
    /* The secondary button.  A context menu opened by the same press
     * that selects would fire every time you clicked anything. */
    bool secondary;
};

#define RSD_KEY_ENTER 1U
#define RSD_KEY_BACKSPACE 2U
#define RSD_KEY_TAB 3U
#define RSD_KEY_ESCAPE 4U
#define RSD_KEY_F4 5U
/* The launcher selects with these, the way dmenu does. */
#define RSD_KEY_LEFT 6U
#define RSD_KEY_RIGHT 7U

#endif /* RSD_DESKTOP_INPUT_H */
