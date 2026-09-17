/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_ABI_NETWORK_H
#define OPENRFS_ABI_NETWORK_H

#include <openrfs/abi/base.h>

#define OPENRFS_NETWORK_IO_MAX_BYTES 4096U

struct openrfs_ipv4_endpoint {
    uint32_t address;
    uint16_t port;
    uint16_t reserved;
} __attribute__((packed));

struct openrfs_network_io {
    uint32_t size;
    uint32_t version;
    openrfs_handle_t handle;
    uint64_t buffer;
    uint64_t deadline_ns;
    struct openrfs_ipv4_endpoint endpoint;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

enum openrfs_stream_shutdown {
    OPENRFS_SHUTDOWN_WRITE = UINT32_C(1) << 0,
    OPENRFS_SHUTDOWN_READ = UINT32_C(1) << 1
};

_Static_assert(sizeof(struct openrfs_ipv4_endpoint) == 8U,
    "OpenRFS IPv4 endpoint ABI changed");
_Static_assert(sizeof(struct openrfs_network_io) == 48U,
    "OpenRFS network-I/O ABI changed");

#endif
