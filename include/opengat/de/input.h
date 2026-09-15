/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_DE_INPUT_H
#define OPENGAT_DE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <opengat/de/surface.h>

/*
 * Pointer and keyboard events, as the shell receives them.
 *
 * A desktop that draws but does not answer is a picture of a desktop.
 * These are the events; opengat/shell.h is what routes them.
 */

enum opengat_event_kind {
    OPENGAT_EVENT_POINTER_DOWN = 0,
    OPENGAT_EVENT_POINTER_UP,
    OPENGAT_EVENT_POINTER_MOVE,
    OPENGAT_EVENT_KEY
};

/* Modifiers as a set, because Ctrl+Shift+click means something that
 * neither of them means alone. */
#define OPENGAT_MOD_CTRL 0x1U
#define OPENGAT_MOD_SHIFT 0x2U
#define OPENGAT_MOD_ALT 0x4U
#define OPENGAT_MOD_SUPER 0x8U

struct opengat_event {
    enum opengat_event_kind kind;
    uint32_t x;
    uint32_t y;
    uint32_t modifiers;
    char key;               /* printable, or 0 */
    uint32_t special;       /* OPENGAT_KEY_*, or 0 */
    bool double_click;
    /* The secondary button.  A context menu opened by the same press
     * that selects would fire every time you clicked anything. */
    bool secondary;
};

#define OPENGAT_KEY_ENTER 1U
#define OPENGAT_KEY_BACKSPACE 2U
#define OPENGAT_KEY_TAB 3U
#define OPENGAT_KEY_ESCAPE 4U
#define OPENGAT_KEY_F4 5U

#endif /* OPENGAT_DE_INPUT_H */
