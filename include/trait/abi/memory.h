/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_ABI_MEMORY_H
#define TRAIT_ABI_MEMORY_H

#include <trait/abi/base.h>

enum trait_memory_flags {
    TRAIT_MEMORY_READ = UINT32_C(1) << 0,
    TRAIT_MEMORY_WRITE = UINT32_C(1) << 1,
    TRAIT_MEMORY_GUARD_BEFORE = UINT32_C(1) << 2,
    TRAIT_MEMORY_GUARD_AFTER = UINT32_C(1) << 3
};

#define TRAIT_MEMORY_FLAGS_V1 (TRAIT_MEMORY_READ | TRAIT_MEMORY_WRITE | \
    TRAIT_MEMORY_GUARD_BEFORE | TRAIT_MEMORY_GUARD_AFTER)

struct trait_memory_map_request {
    uint32_t size;
    uint32_t version;
    uint64_t length;
    uint64_t address_hint;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

struct trait_memory_map_response {
    uint32_t size;
    uint32_t version;
    uint64_t address;
    uint64_t length;
} __attribute__((packed));

_Static_assert(sizeof(struct trait_memory_map_request) == 32U,
    "Trait OS memory-map request ABI changed");
_Static_assert(sizeof(struct trait_memory_map_response) == 24U,
    "Trait OS memory-map response ABI changed");

#endif
