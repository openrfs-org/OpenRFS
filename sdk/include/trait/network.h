/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_USER_NETWORK_H
#define TRAIT_USER_NETWORK_H

#include <stddef.h>
#include <stdint.h>
#include <trait/abi.h>

long trait_dns_resolve(const char *hostname, uint64_t deadline_ns);
long trait_stream_open(void);
long trait_stream_connect(trait_handle_t stream,
    const struct trait_ipv4_endpoint *endpoint, uint64_t deadline_ns);
long trait_stream_read(trait_handle_t stream, void *buffer, size_t length,
    uint64_t deadline_ns);
long trait_stream_write(trait_handle_t stream, const void *buffer,
    size_t length, uint64_t deadline_ns);
long trait_stream_shutdown(trait_handle_t stream, uint32_t flags,
    uint64_t deadline_ns);
long trait_datagram_open(void);
long trait_datagram_bind(trait_handle_t datagram, uint16_t port);
long trait_datagram_send(trait_handle_t datagram,
    const struct trait_ipv4_endpoint *destination, const void *buffer,
    size_t length, uint64_t deadline_ns);
long trait_datagram_receive(trait_handle_t datagram,
    struct trait_ipv4_endpoint *source, void *buffer, size_t length,
    uint64_t deadline_ns);
long trait_network_address(trait_handle_t handle, int peer,
    struct trait_ipv4_endpoint *endpoint);
long trait_network_cancel(trait_handle_t handle);

#endif
