/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_USER_NETWORK_H
#define OPENGAT_USER_NETWORK_H

#include <stddef.h>
#include <stdint.h>
#include <opengat/abi.h>

long opengat_dns_resolve(const char *hostname, uint64_t deadline_ns);
long opengat_stream_open(void);
long opengat_stream_connect(opengat_handle_t stream,
    const struct opengat_ipv4_endpoint *endpoint, uint64_t deadline_ns);
long opengat_stream_read(opengat_handle_t stream, void *buffer, size_t length,
    uint64_t deadline_ns);
long opengat_stream_write(opengat_handle_t stream, const void *buffer,
    size_t length, uint64_t deadline_ns);
long opengat_stream_shutdown(opengat_handle_t stream, uint32_t flags,
    uint64_t deadline_ns);
long opengat_datagram_open(void);
long opengat_datagram_bind(opengat_handle_t datagram, uint16_t port);
long opengat_datagram_send(opengat_handle_t datagram,
    const struct opengat_ipv4_endpoint *destination, const void *buffer,
    size_t length, uint64_t deadline_ns);
long opengat_datagram_receive(opengat_handle_t datagram,
    struct opengat_ipv4_endpoint *source, void *buffer, size_t length,
    uint64_t deadline_ns);
long opengat_network_address(opengat_handle_t handle, int peer,
    struct opengat_ipv4_endpoint *endpoint);
long opengat_network_cancel(opengat_handle_t handle);

#endif
