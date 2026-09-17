/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_ABI_WINDOW_H
#define OPENRFS_ABI_WINDOW_H

#include <openrfs/abi/base.h>

#define OPENRFS_WINDOW_TITLE_MAX 31U
#define OPENRFS_DAMAGE_MAX 8U
#define OPENRFS_PIXEL_XRGB8888 UINT32_C(1)

struct openrfs_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct openrfs_window_create_request {
    uint32_t size;
    uint32_t version;
    uint64_t title;
    uint32_t title_length;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

struct openrfs_window_create_response {
    uint32_t size;
    uint32_t version;
    openrfs_handle_t window;
    openrfs_handle_t events;
    uint64_t surface_address;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t pixel_format;
} __attribute__((packed));

struct openrfs_present_request {
    uint32_t size;
    uint32_t version;
    openrfs_handle_t window;
    uint64_t rectangles;
    uint32_t rectangle_count;
    uint32_t flags;
} __attribute__((packed));

enum openrfs_event_type {
    OPENRFS_EVENT_NONE = 0,
    OPENRFS_EVENT_KEY = 1,
    OPENRFS_EVENT_POINTER_MOVE = 2,
    OPENRFS_EVENT_POINTER_BUTTON = 3,
    OPENRFS_EVENT_FOCUS = 4,
    OPENRFS_EVENT_CLOSE = 5,
    OPENRFS_EVENT_QUEUE_OVERFLOW = 6
};

enum openrfs_key_action {
    OPENRFS_KEY_RELEASED = 0,
    OPENRFS_KEY_PRESSED = 1,
    OPENRFS_KEY_REPEATED = 2
};

struct openrfs_event {
    uint32_t size;
    uint32_t version;
    uint32_t type;
    uint32_t flags;
    uint64_t monotonic_ns;
    int32_t x;
    int32_t y;
    int32_t delta_x;
    int32_t delta_y;
    uint32_t code;
    uint32_t value;
    uint32_t modifiers;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct openrfs_rect) == 16U,
    "OpenRFS rectangle ABI changed");
_Static_assert(sizeof(struct openrfs_window_create_request) == 40U,
    "OpenRFS window-create request ABI changed");
_Static_assert(sizeof(struct openrfs_window_create_response) == 48U,
    "OpenRFS window-create response ABI changed");
_Static_assert(sizeof(struct openrfs_present_request) == 32U,
    "OpenRFS present ABI changed");
_Static_assert(sizeof(struct openrfs_event) == 56U,
    "OpenRFS event ABI changed");

#endif
