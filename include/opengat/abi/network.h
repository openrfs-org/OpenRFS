/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_ABI_NETWORK_H
#define OPENGAT_ABI_NETWORK_H

#include <opengat/abi/base.h>

#define OPENGAT_NETWORK_IO_MAX_BYTES 4096U

struct opengat_ipv4_endpoint {
    uint32_t address;
    uint16_t port;
    uint16_t reserved;
} __attribute__((packed));

struct opengat_network_io {
    uint32_t size;
    uint32_t version;
    opengat_handle_t handle;
    uint64_t buffer;
    uint64_t deadline_ns;
    struct opengat_ipv4_endpoint endpoint;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

enum opengat_stream_shutdown {
    OPENGAT_SHUTDOWN_WRITE = UINT32_C(1) << 0,
    OPENGAT_SHUTDOWN_READ = UINT32_C(1) << 1
};

_Static_assert(sizeof(struct opengat_ipv4_endpoint) == 8U,
    "OpenGAT IPv4 endpoint ABI changed");
_Static_assert(sizeof(struct opengat_network_io) == 48U,
    "OpenGAT network-I/O ABI changed");

#endif
