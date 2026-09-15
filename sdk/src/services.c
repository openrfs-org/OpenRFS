/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/event.h>
#include <trait/network.h>
#include <trait/runtime.h>
#include <trait/window.h>

#include <errno.h>
#include <string.h>

long trait_wait(struct trait_wait_item *items, size_t count,
    uint64_t deadline_ns)
{
    const struct trait_wait_request request = {sizeof(request),
        TRAIT_ABI_VERSION, (uint64_t)(uintptr_t)items, deadline_ns,
        (uint32_t)count, 0U};

    if (items == NULL || count == 0U || count > TRAIT_WAIT_MAX) {
        return -TRAIT_EINVAL;
    }
    return trait_syscall1(TRAIT_SYS_WAIT,
        (uint64_t)(uintptr_t)&request);
}

long trait_timer_create(void)
{
    return trait_syscall0(TRAIT_SYS_TIMER_CREATE);
}

long trait_timer_set(trait_handle_t timer, uint64_t deadline_ns)
{
    const struct trait_timer_set_request request = {sizeof(request),
        TRAIT_ABI_VERSION, timer, deadline_ns, 0U, 0U};

    return trait_syscall1(TRAIT_SYS_TIMER_SET,
        (uint64_t)(uintptr_t)&request);
}

long trait_cancel(trait_handle_t handle)
{
    return trait_syscall1(TRAIT_SYS_CANCEL, handle);
}

int trait_window_create(const char *title, uint32_t width, uint32_t height,
    struct trait_window_create_response *response)
{
    if (title == NULL) {
        return trait_result(-TRAIT_EFAULT);
    }
    const struct trait_window_create_request request = {
        sizeof(request), TRAIT_ABI_VERSION, (uint64_t)(uintptr_t)title,
        (uint32_t)strlen(title), width, height, TRAIT_PIXEL_XRGB8888, 0U, 0U
    };
    return trait_result(trait_syscall2(TRAIT_SYS_WINDOW_CREATE,
        (uint64_t)(uintptr_t)&request, (uint64_t)(uintptr_t)response));
}
long trait_surface_present(trait_handle_t window,
    const struct trait_rect *rectangles, size_t count)
{
    const struct trait_present_request request = {sizeof(request),
        TRAIT_ABI_VERSION, window, (uint64_t)(uintptr_t)rectangles,
        (uint32_t)count, 0U};
    return trait_syscall1(TRAIT_SYS_SURFACE_PRESENT,
        (uint64_t)(uintptr_t)&request);
}
long trait_event_read(trait_handle_t events, struct trait_event *event)
{ return trait_syscall2(TRAIT_SYS_EVENT_READ, events, (uint64_t)(uintptr_t)event); }
long trait_event_wait(trait_handle_t events, uint64_t deadline_ns)
{
    struct trait_wait_item item = {events, TRAIT_WAIT_READABLE, 0U};
    const struct trait_wait_request request = {sizeof(request),
        TRAIT_ABI_VERSION, (uint64_t)(uintptr_t)&item, deadline_ns, 1U, 0U};
    return trait_syscall1(TRAIT_SYS_WAIT, (uint64_t)(uintptr_t)&request);
}
long trait_pointer_capture(trait_handle_t window, int capture)
{ return trait_syscall2(TRAIT_SYS_POINTER_CAPTURE, window, capture != 0); }

long trait_dns_resolve(const char *hostname, uint64_t deadline_ns)
{
    if (hostname == NULL) return -TRAIT_EFAULT;
    return trait_syscall3(TRAIT_SYS_DNS_RESOLVE,
        (uint64_t)(uintptr_t)hostname, strlen(hostname), deadline_ns);
}
long trait_stream_open(void) { return trait_syscall0(TRAIT_SYS_STREAM_OPEN); }
long trait_stream_connect(trait_handle_t stream,
    const struct trait_ipv4_endpoint *endpoint, uint64_t deadline_ns)
{ return trait_syscall3(TRAIT_SYS_STREAM_CONNECT, stream, (uint64_t)(uintptr_t)endpoint, deadline_ns); }
static long network_io(uint64_t number, trait_handle_t handle, void *buffer,
    size_t length, uint64_t deadline, struct trait_ipv4_endpoint *endpoint)
{
    struct trait_network_io request;

    if (length == 0U || length > UINT32_MAX) {
        return -TRAIT_EINVAL;
    }
    if ((number == TRAIT_SYS_STREAM_READ ||
            number == TRAIT_SYS_STREAM_WRITE) &&
        length > TRAIT_NETWORK_IO_MAX_BYTES) {
        length = TRAIT_NETWORK_IO_MAX_BYTES;
    }
    request = (struct trait_network_io){sizeof(request), TRAIT_ABI_VERSION,
        handle, (uint64_t)(uintptr_t)buffer, deadline, {0U, 0U, 0U},
        (uint32_t)length, 0U};
    if (endpoint != NULL) request.endpoint = *endpoint;
    const long result = trait_syscall1(number, (uint64_t)(uintptr_t)&request);
    if (result >= 0 && endpoint != NULL &&
        number == TRAIT_SYS_DATAGRAM_RECEIVE) *endpoint = request.endpoint;
    return result;
}
long trait_stream_read(trait_handle_t stream, void *buffer, size_t length,
    uint64_t deadline_ns)
{ return network_io(TRAIT_SYS_STREAM_READ, stream, buffer, length, deadline_ns, NULL); }
long trait_stream_write(trait_handle_t stream, const void *buffer,
    size_t length, uint64_t deadline_ns)
{ return network_io(TRAIT_SYS_STREAM_WRITE, stream, (void *)(uintptr_t)buffer, length, deadline_ns, NULL); }
long trait_stream_shutdown(trait_handle_t stream, uint32_t flags,
    uint64_t deadline_ns)
{ return trait_syscall3(TRAIT_SYS_STREAM_SHUTDOWN, stream, flags, deadline_ns); }
long trait_datagram_open(void) { return trait_syscall0(TRAIT_SYS_DATAGRAM_OPEN); }
long trait_datagram_bind(trait_handle_t datagram, uint16_t port)
{ return trait_syscall2(TRAIT_SYS_DATAGRAM_BIND, datagram, port); }
long trait_datagram_send(trait_handle_t datagram,
    const struct trait_ipv4_endpoint *destination, const void *buffer,
    size_t length, uint64_t deadline_ns)
{
    if (destination == NULL) return -TRAIT_EFAULT;
    struct trait_ipv4_endpoint endpoint = *destination;
    return network_io(TRAIT_SYS_DATAGRAM_SEND, datagram,
        (void *)(uintptr_t)buffer, length, deadline_ns, &endpoint);
}
long trait_datagram_receive(trait_handle_t datagram,
    struct trait_ipv4_endpoint *source, void *buffer, size_t length,
    uint64_t deadline_ns)
{ return network_io(TRAIT_SYS_DATAGRAM_RECEIVE, datagram, buffer, length, deadline_ns, source); }
long trait_network_address(trait_handle_t handle, int peer,
    struct trait_ipv4_endpoint *endpoint)
{ return trait_syscall3(TRAIT_SYS_NETWORK_ADDRESS, handle, peer != 0, (uint64_t)(uintptr_t)endpoint); }
long trait_network_cancel(trait_handle_t handle)
{ return trait_syscall1(TRAIT_SYS_CANCEL, handle); }
