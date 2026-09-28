/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_ABI_WINDOW_H
#define RSD_ABI_WINDOW_H

#include <rsd/abi/base.h>

#define RSD_WINDOW_TITLE_MAX 31U
#define RSD_DAMAGE_MAX 8U
#define RSD_PIXEL_XRGB8888 UINT32_C(1)

struct rsd_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct rsd_window_create_request {
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

struct rsd_window_create_response {
    uint32_t size;
    uint32_t version;
    rsd_handle_t window;
    rsd_handle_t events;
    uint64_t surface_address;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t pixel_format;
} __attribute__((packed));

struct rsd_present_request {
    uint32_t size;
    uint32_t version;
    rsd_handle_t window;
    uint64_t rectangles;
    uint32_t rectangle_count;
    uint32_t flags;
} __attribute__((packed));

enum rsd_event_type {
    RSD_EVENT_NONE = 0,
    RSD_EVENT_KEY = 1,
    RSD_EVENT_POINTER_MOVE = 2,
    RSD_EVENT_POINTER_BUTTON = 3,
    RSD_EVENT_FOCUS = 4,
    RSD_EVENT_CLOSE = 5,
    RSD_EVENT_QUEUE_OVERFLOW = 6
};

enum rsd_key_action {
    RSD_KEY_RELEASED = 0,
    RSD_KEY_PRESSED = 1,
    RSD_KEY_REPEATED = 2
};

struct rsd_event {
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

_Static_assert(sizeof(struct rsd_rect) == 16U,
    "RSD rectangle ABI changed");
_Static_assert(sizeof(struct rsd_window_create_request) == 40U,
    "RSD window-create request ABI changed");
_Static_assert(sizeof(struct rsd_window_create_response) == 48U,
    "RSD window-create response ABI changed");
_Static_assert(sizeof(struct rsd_present_request) == 32U,
    "RSD present ABI changed");
_Static_assert(sizeof(struct rsd_event) == 56U,
    "RSD event ABI changed");

#endif
