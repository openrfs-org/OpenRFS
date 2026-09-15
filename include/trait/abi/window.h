/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_ABI_WINDOW_H
#define TRAIT_ABI_WINDOW_H

#include <trait/abi/base.h>

#define TRAIT_WINDOW_TITLE_MAX 31U
#define TRAIT_DAMAGE_MAX 8U
#define TRAIT_PIXEL_XRGB8888 UINT32_C(1)

struct trait_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct trait_window_create_request {
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

struct trait_window_create_response {
    uint32_t size;
    uint32_t version;
    trait_handle_t window;
    trait_handle_t events;
    uint64_t surface_address;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t pixel_format;
} __attribute__((packed));

struct trait_present_request {
    uint32_t size;
    uint32_t version;
    trait_handle_t window;
    uint64_t rectangles;
    uint32_t rectangle_count;
    uint32_t flags;
} __attribute__((packed));

enum trait_event_type {
    TRAIT_EVENT_NONE = 0,
    TRAIT_EVENT_KEY = 1,
    TRAIT_EVENT_POINTER_MOVE = 2,
    TRAIT_EVENT_POINTER_BUTTON = 3,
    TRAIT_EVENT_FOCUS = 4,
    TRAIT_EVENT_CLOSE = 5,
    TRAIT_EVENT_QUEUE_OVERFLOW = 6
};

enum trait_key_action {
    TRAIT_KEY_RELEASED = 0,
    TRAIT_KEY_PRESSED = 1,
    TRAIT_KEY_REPEATED = 2
};

struct trait_event {
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

_Static_assert(sizeof(struct trait_rect) == 16U,
    "Trait OS rectangle ABI changed");
_Static_assert(sizeof(struct trait_window_create_request) == 40U,
    "Trait OS window-create request ABI changed");
_Static_assert(sizeof(struct trait_window_create_response) == 48U,
    "Trait OS window-create response ABI changed");
_Static_assert(sizeof(struct trait_present_request) == 32U,
    "Trait OS present ABI changed");
_Static_assert(sizeof(struct trait_event) == 56U,
    "Trait OS event ABI changed");

#endif
