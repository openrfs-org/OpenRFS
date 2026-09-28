/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/event.h>
#include <rsd/network.h>
#include <rsd/runtime.h>
#include <rsd/window.h>

#include <errno.h>
#include <string.h>

long rsd_wait(struct rsd_wait_item *items, size_t count,
    uint64_t deadline_ns)
{
    const struct rsd_wait_request request = {sizeof(request),
        RSD_ABI_VERSION, (uint64_t)(uintptr_t)items, deadline_ns,
        (uint32_t)count, 0U};

    if (items == NULL || count == 0U || count > RSD_WAIT_MAX) {
        return -RSD_EINVAL;
    }
    return rsd_syscall1(RSD_SYS_WAIT,
        (uint64_t)(uintptr_t)&request);
}

long rsd_timer_create(void)
{
    return rsd_syscall0(RSD_SYS_TIMER_CREATE);
}

long rsd_timer_set(rsd_handle_t timer, uint64_t deadline_ns)
{
    const struct rsd_timer_set_request request = {sizeof(request),
        RSD_ABI_VERSION, timer, deadline_ns, 0U, 0U};

    return rsd_syscall1(RSD_SYS_TIMER_SET,
        (uint64_t)(uintptr_t)&request);
}

long rsd_cancel(rsd_handle_t handle)
{
    return rsd_syscall1(RSD_SYS_CANCEL, handle);
}

int rsd_window_create(const char *title, uint32_t width, uint32_t height,
    struct rsd_window_create_response *response)
{
    if (title == NULL) {
        return rsd_result(-RSD_EFAULT);
    }
    const struct rsd_window_create_request request = {
        sizeof(request), RSD_ABI_VERSION, (uint64_t)(uintptr_t)title,
        (uint32_t)strlen(title), width, height, RSD_PIXEL_XRGB8888, 0U, 0U
    };
    return rsd_result(rsd_syscall2(RSD_SYS_WINDOW_CREATE,
        (uint64_t)(uintptr_t)&request, (uint64_t)(uintptr_t)response));
}
long rsd_surface_present(rsd_handle_t window,
    const struct rsd_rect *rectangles, size_t count)
{
    const struct rsd_present_request request = {sizeof(request),
        RSD_ABI_VERSION, window, (uint64_t)(uintptr_t)rectangles,
        (uint32_t)count, 0U};
    return rsd_syscall1(RSD_SYS_SURFACE_PRESENT,
        (uint64_t)(uintptr_t)&request);
}
long rsd_event_read(rsd_handle_t events, struct rsd_event *event)
{ return rsd_syscall2(RSD_SYS_EVENT_READ, events, (uint64_t)(uintptr_t)event); }
long rsd_event_wait(rsd_handle_t events, uint64_t deadline_ns)
{
    struct rsd_wait_item item = {events, RSD_WAIT_READABLE, 0U};
    const struct rsd_wait_request request = {sizeof(request),
        RSD_ABI_VERSION, (uint64_t)(uintptr_t)&item, deadline_ns, 1U, 0U};
    return rsd_syscall1(RSD_SYS_WAIT, (uint64_t)(uintptr_t)&request);
}
long rsd_pointer_capture(rsd_handle_t window, int capture)
{ return rsd_syscall2(RSD_SYS_POINTER_CAPTURE, window, capture != 0); }

long rsd_dns_resolve(const char *hostname, uint64_t deadline_ns)
{
    if (hostname == NULL) return -RSD_EFAULT;
    return rsd_syscall3(RSD_SYS_DNS_RESOLVE,
        (uint64_t)(uintptr_t)hostname, strlen(hostname), deadline_ns);
}
long rsd_stream_open(void) { return rsd_syscall0(RSD_SYS_STREAM_OPEN); }
long rsd_stream_connect(rsd_handle_t stream,
    const struct rsd_ipv4_endpoint *endpoint, uint64_t deadline_ns)
{ return rsd_syscall3(RSD_SYS_STREAM_CONNECT, stream, (uint64_t)(uintptr_t)endpoint, deadline_ns); }
static long network_io(uint64_t number, rsd_handle_t handle, void *buffer,
    size_t length, uint64_t deadline, struct rsd_ipv4_endpoint *endpoint)
{
    struct rsd_network_io request;

    if (length == 0U || length > UINT32_MAX) {
        return -RSD_EINVAL;
    }
    if ((number == RSD_SYS_STREAM_READ ||
            number == RSD_SYS_STREAM_WRITE) &&
        length > RSD_NETWORK_IO_MAX_BYTES) {
        length = RSD_NETWORK_IO_MAX_BYTES;
    }
    request = (struct rsd_network_io){sizeof(request), RSD_ABI_VERSION,
        handle, (uint64_t)(uintptr_t)buffer, deadline, {0U, 0U, 0U},
        (uint32_t)length, 0U};
    if (endpoint != NULL) request.endpoint = *endpoint;
    const long result = rsd_syscall1(number, (uint64_t)(uintptr_t)&request);
    if (result >= 0 && endpoint != NULL &&
        number == RSD_SYS_DATAGRAM_RECEIVE) *endpoint = request.endpoint;
    return result;
}
long rsd_stream_read(rsd_handle_t stream, void *buffer, size_t length,
    uint64_t deadline_ns)
{ return network_io(RSD_SYS_STREAM_READ, stream, buffer, length, deadline_ns, NULL); }
long rsd_stream_write(rsd_handle_t stream, const void *buffer,
    size_t length, uint64_t deadline_ns)
{ return network_io(RSD_SYS_STREAM_WRITE, stream, (void *)(uintptr_t)buffer, length, deadline_ns, NULL); }
long rsd_stream_shutdown(rsd_handle_t stream, uint32_t flags,
    uint64_t deadline_ns)
{ return rsd_syscall3(RSD_SYS_STREAM_SHUTDOWN, stream, flags, deadline_ns); }
long rsd_datagram_open(void) { return rsd_syscall0(RSD_SYS_DATAGRAM_OPEN); }
long rsd_datagram_bind(rsd_handle_t datagram, uint16_t port)
{ return rsd_syscall2(RSD_SYS_DATAGRAM_BIND, datagram, port); }
long rsd_datagram_send(rsd_handle_t datagram,
    const struct rsd_ipv4_endpoint *destination, const void *buffer,
    size_t length, uint64_t deadline_ns)
{
    if (destination == NULL) return -RSD_EFAULT;
    struct rsd_ipv4_endpoint endpoint = *destination;
    return network_io(RSD_SYS_DATAGRAM_SEND, datagram,
        (void *)(uintptr_t)buffer, length, deadline_ns, &endpoint);
}
long rsd_datagram_receive(rsd_handle_t datagram,
    struct rsd_ipv4_endpoint *source, void *buffer, size_t length,
    uint64_t deadline_ns)
{ return network_io(RSD_SYS_DATAGRAM_RECEIVE, datagram, buffer, length, deadline_ns, source); }
long rsd_network_address(rsd_handle_t handle, int peer,
    struct rsd_ipv4_endpoint *endpoint)
{ return rsd_syscall3(RSD_SYS_NETWORK_ADDRESS, handle, peer != 0, (uint64_t)(uintptr_t)endpoint); }
long rsd_network_cancel(rsd_handle_t handle)
{ return rsd_syscall1(RSD_SYS_CANCEL, handle); }
