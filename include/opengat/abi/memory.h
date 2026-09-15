/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_ABI_MEMORY_H
#define OPENGAT_ABI_MEMORY_H

#include <opengat/abi/base.h>

enum opengat_memory_flags {
    OPENGAT_MEMORY_READ = UINT32_C(1) << 0,
    OPENGAT_MEMORY_WRITE = UINT32_C(1) << 1,
    OPENGAT_MEMORY_GUARD_BEFORE = UINT32_C(1) << 2,
    OPENGAT_MEMORY_GUARD_AFTER = UINT32_C(1) << 3
};

#define OPENGAT_MEMORY_FLAGS_V1 (OPENGAT_MEMORY_READ | OPENGAT_MEMORY_WRITE | \
    OPENGAT_MEMORY_GUARD_BEFORE | OPENGAT_MEMORY_GUARD_AFTER)

struct opengat_memory_map_request {
    uint32_t size;
    uint32_t version;
    uint64_t length;
    uint64_t address_hint;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

struct opengat_memory_map_response {
    uint32_t size;
    uint32_t version;
    uint64_t address;
    uint64_t length;
} __attribute__((packed));

_Static_assert(sizeof(struct opengat_memory_map_request) == 32U,
    "OpenGAT memory-map request ABI changed");
_Static_assert(sizeof(struct opengat_memory_map_response) == 24U,
    "OpenGAT memory-map response ABI changed");

#endif
