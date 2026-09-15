/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_ABI_NETWORK_H
#define TRAIT_ABI_NETWORK_H

#include <trait/abi/base.h>

#define TRAIT_NETWORK_IO_MAX_BYTES 4096U

struct trait_ipv4_endpoint {
    uint32_t address;
    uint16_t port;
    uint16_t reserved;
} __attribute__((packed));

struct trait_network_io {
    uint32_t size;
    uint32_t version;
    trait_handle_t handle;
    uint64_t buffer;
    uint64_t deadline_ns;
    struct trait_ipv4_endpoint endpoint;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

enum trait_stream_shutdown {
    TRAIT_SHUTDOWN_WRITE = UINT32_C(1) << 0,
    TRAIT_SHUTDOWN_READ = UINT32_C(1) << 1
};

_Static_assert(sizeof(struct trait_ipv4_endpoint) == 8U,
    "Trait OS IPv4 endpoint ABI changed");
_Static_assert(sizeof(struct trait_network_io) == 48U,
    "Trait OS network-I/O ABI changed");

#endif
