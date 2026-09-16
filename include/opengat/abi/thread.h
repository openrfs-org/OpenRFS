/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_ABI_THREAD_H
#define OPENGAT_ABI_THREAD_H

#include <opengat/abi/base.h>

struct opengat_thread_create_request {
    uint32_t size;
    uint32_t version;
    uint64_t entry;
    uint64_t argument;
    uint64_t tls_base;
    uint32_t stack_bytes;
    uint32_t flags;
} __attribute__((packed));

struct opengat_futex_request {
    uint32_t size;
    uint32_t version;
    uint64_t address;
    uint64_t deadline_ns;
    uint32_t expected;
    uint32_t count;
} __attribute__((packed));

_Static_assert(sizeof(struct opengat_thread_create_request) == 40U,
    "OpenGAT thread-create ABI changed");
_Static_assert(sizeof(struct opengat_futex_request) == 32U,
    "OpenGAT futex ABI changed");

#endif
