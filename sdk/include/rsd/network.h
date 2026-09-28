/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_USER_NETWORK_H
#define RSD_USER_NETWORK_H

#include <stddef.h>
#include <stdint.h>
#include <rsd/abi.h>

long rsd_dns_resolve(const char *hostname, uint64_t deadline_ns);
long rsd_stream_open(void);
long rsd_stream_connect(rsd_handle_t stream,
    const struct rsd_ipv4_endpoint *endpoint, uint64_t deadline_ns);
long rsd_stream_read(rsd_handle_t stream, void *buffer, size_t length,
    uint64_t deadline_ns);
long rsd_stream_write(rsd_handle_t stream, const void *buffer,
    size_t length, uint64_t deadline_ns);
long rsd_stream_shutdown(rsd_handle_t stream, uint32_t flags,
    uint64_t deadline_ns);
long rsd_datagram_open(void);
long rsd_datagram_bind(rsd_handle_t datagram, uint16_t port);
long rsd_datagram_send(rsd_handle_t datagram,
    const struct rsd_ipv4_endpoint *destination, const void *buffer,
    size_t length, uint64_t deadline_ns);
long rsd_datagram_receive(rsd_handle_t datagram,
    struct rsd_ipv4_endpoint *source, void *buffer, size_t length,
    uint64_t deadline_ns);
long rsd_network_address(rsd_handle_t handle, int peer,
    struct rsd_ipv4_endpoint *endpoint);
long rsd_network_cancel(rsd_handle_t handle);

#endif
