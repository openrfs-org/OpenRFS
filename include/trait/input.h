/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_INPUT_H
#define TRAIT_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <trait/surface.h>

/*
 * Pointer and keyboard events, as the shell receives them.
 *
 * A desktop that draws but does not answer is a picture of a desktop.
 * These are the events; trait/shell.h is what routes them.
 */

enum trait_event_kind {
    TRAIT_EVENT_POINTER_DOWN = 0,
    TRAIT_EVENT_POINTER_UP,
    TRAIT_EVENT_POINTER_MOVE,
    TRAIT_EVENT_KEY
};

/* Modifiers as a set, because Ctrl+Shift+click means something that
 * neither of them means alone. */
#define TRAIT_MOD_CTRL 0x1U
#define TRAIT_MOD_SHIFT 0x2U
#define TRAIT_MOD_ALT 0x4U
#define TRAIT_MOD_SUPER 0x8U

struct trait_event {
    enum trait_event_kind kind;
    uint32_t x;
    uint32_t y;
    uint32_t modifiers;
    char key;               /* printable, or 0 */
    uint32_t special;       /* TRAIT_KEY_*, or 0 */
    bool double_click;
    /* The secondary button.  A context menu opened by the same press
     * that selects would fire every time you clicked anything. */
    bool secondary;
};

#define TRAIT_KEY_ENTER 1U
#define TRAIT_KEY_BACKSPACE 2U
#define TRAIT_KEY_TAB 3U
#define TRAIT_KEY_ESCAPE 4U
#define TRAIT_KEY_F4 5U
/* The launcher selects with these, the way dmenu does. */
#define TRAIT_KEY_LEFT 6U
#define TRAIT_KEY_RIGHT 7U

#endif /* TRAIT_INPUT_H */
