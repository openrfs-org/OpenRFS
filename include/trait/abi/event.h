/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_ABI_EVENT_H
#define TRAIT_ABI_EVENT_H

#include <trait/abi/base.h>

#define TRAIT_WAIT_MAX 8U

enum trait_wait_interest {
    TRAIT_WAIT_READABLE = UINT32_C(1) << 0,
    TRAIT_WAIT_WRITABLE = UINT32_C(1) << 1,
    TRAIT_WAIT_ACCEPTABLE = UINT32_C(1) << 2,
    TRAIT_WAIT_SIGNALED = UINT32_C(1) << 3,
    TRAIT_WAIT_CLOSED = UINT32_C(1) << 4
};

#define TRAIT_WAIT_INTERESTS_V1 ((UINT32_C(1) << 5) - UINT32_C(1))

struct trait_wait_item {
    trait_handle_t handle;
    uint32_t interests;
    uint32_t ready;
} __attribute__((packed));

struct trait_wait_request {
    uint32_t size;
    uint32_t version;
    uint64_t items;
    uint64_t deadline_ns;
    uint32_t count;
    uint32_t flags;
} __attribute__((packed));

struct trait_timer_set_request {
    uint32_t size;
    uint32_t version;
    trait_handle_t handle;
    uint64_t deadline_ns;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct trait_wait_item) == 16U,
    "Trait OS wait-item ABI changed");
_Static_assert(sizeof(struct trait_wait_request) == 32U,
    "Trait OS wait-request ABI changed");
_Static_assert(sizeof(struct trait_timer_set_request) == 32U,
    "Trait OS timer-set ABI changed");

#endif
