/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_USER_NETWORK_H
#define OPENRFS_USER_NETWORK_H

#include <stddef.h>
#include <stdint.h>
#include <openrfs/abi.h>

long openrfs_dns_resolve(const char *hostname, uint64_t deadline_ns);
long openrfs_stream_open(void);
long openrfs_stream_connect(openrfs_handle_t stream,
    const struct openrfs_ipv4_endpoint *endpoint, uint64_t deadline_ns);
long openrfs_stream_read(openrfs_handle_t stream, void *buffer, size_t length,
    uint64_t deadline_ns);
long openrfs_stream_write(openrfs_handle_t stream, const void *buffer,
    size_t length, uint64_t deadline_ns);
long openrfs_stream_shutdown(openrfs_handle_t stream, uint32_t flags,
    uint64_t deadline_ns);
long openrfs_datagram_open(void);
long openrfs_datagram_bind(openrfs_handle_t datagram, uint16_t port);
long openrfs_datagram_send(openrfs_handle_t datagram,
    const struct openrfs_ipv4_endpoint *destination, const void *buffer,
    size_t length, uint64_t deadline_ns);
long openrfs_datagram_receive(openrfs_handle_t datagram,
    struct openrfs_ipv4_endpoint *source, void *buffer, size_t length,
    uint64_t deadline_ns);
long openrfs_network_address(openrfs_handle_t handle, int peer,
    struct openrfs_ipv4_endpoint *endpoint);
long openrfs_network_cancel(openrfs_handle_t handle);

#endif
