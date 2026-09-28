/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_ABI_MEMORY_H
#define RSD_ABI_MEMORY_H

#include <rsd/abi/base.h>

enum rsd_memory_flags {
    RSD_MEMORY_READ = UINT32_C(1) << 0,
    RSD_MEMORY_WRITE = UINT32_C(1) << 1,
    RSD_MEMORY_GUARD_BEFORE = UINT32_C(1) << 2,
    RSD_MEMORY_GUARD_AFTER = UINT32_C(1) << 3
};

#define RSD_MEMORY_FLAGS_V1 (RSD_MEMORY_READ | RSD_MEMORY_WRITE | \
    RSD_MEMORY_GUARD_BEFORE | RSD_MEMORY_GUARD_AFTER)

struct rsd_memory_map_request {
    uint32_t size;
    uint32_t version;
    uint64_t length;
    uint64_t address_hint;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

struct rsd_memory_map_response {
    uint32_t size;
    uint32_t version;
    uint64_t address;
    uint64_t length;
} __attribute__((packed));

_Static_assert(sizeof(struct rsd_memory_map_request) == 32U,
    "RSD memory-map request ABI changed");
_Static_assert(sizeof(struct rsd_memory_map_response) == 24U,
    "RSD memory-map response ABI changed");

#endif
