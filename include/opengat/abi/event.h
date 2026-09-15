/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_ABI_EVENT_H
#define OPENGAT_ABI_EVENT_H

#include <opengat/abi/base.h>

#define OPENGAT_WAIT_MAX 8U

enum opengat_wait_interest {
    OPENGAT_WAIT_READABLE = UINT32_C(1) << 0,
    OPENGAT_WAIT_WRITABLE = UINT32_C(1) << 1,
    OPENGAT_WAIT_ACCEPTABLE = UINT32_C(1) << 2,
    OPENGAT_WAIT_SIGNALED = UINT32_C(1) << 3,
    OPENGAT_WAIT_CLOSED = UINT32_C(1) << 4
};

#define OPENGAT_WAIT_INTERESTS_V1 ((UINT32_C(1) << 5) - UINT32_C(1))

struct opengat_wait_item {
    opengat_handle_t handle;
    uint32_t interests;
    uint32_t ready;
} __attribute__((packed));

struct opengat_wait_request {
    uint32_t size;
    uint32_t version;
    uint64_t items;
    uint64_t deadline_ns;
    uint32_t count;
    uint32_t flags;
} __attribute__((packed));

struct opengat_timer_set_request {
    uint32_t size;
    uint32_t version;
    opengat_handle_t handle;
    uint64_t deadline_ns;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct opengat_wait_item) == 16U,
    "OpenGAT wait-item ABI changed");
_Static_assert(sizeof(struct opengat_wait_request) == 32U,
    "OpenGAT wait-request ABI changed");
_Static_assert(sizeof(struct opengat_timer_set_request) == 32U,
    "OpenGAT timer-set ABI changed");

#endif
