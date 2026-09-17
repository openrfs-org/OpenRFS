/* SPDX-License-Identifier: GPL-3.0-only */
#include <openrfs/event.h>
#include <openrfs/network.h>
#include <openrfs/runtime.h>
#include <openrfs/window.h>

#include <errno.h>
#include <string.h>

long openrfs_wait(struct openrfs_wait_item *items, size_t count,
    uint64_t deadline_ns)
{
    const struct openrfs_wait_request request = {sizeof(request),
        OPENRFS_ABI_VERSION, (uint64_t)(uintptr_t)items, deadline_ns,
        (uint32_t)count, 0U};

    if (items == NULL || count == 0U || count > OPENRFS_WAIT_MAX) {
        return -OPENRFS_EINVAL;
    }
    return openrfs_syscall1(OPENRFS_SYS_WAIT,
        (uint64_t)(uintptr_t)&request);
}

long openrfs_timer_create(void)
{
    return openrfs_syscall0(OPENRFS_SYS_TIMER_CREATE);
}

long openrfs_timer_set(openrfs_handle_t timer, uint64_t deadline_ns)
{
    const struct openrfs_timer_set_request request = {sizeof(request),
        OPENRFS_ABI_VERSION, timer, deadline_ns, 0U, 0U};

    return openrfs_syscall1(OPENRFS_SYS_TIMER_SET,
        (uint64_t)(uintptr_t)&request);
}

long openrfs_cancel(openrfs_handle_t handle)
{
    return openrfs_syscall1(OPENRFS_SYS_CANCEL, handle);
}

int openrfs_window_create(const char *title, uint32_t width, uint32_t height,
    struct openrfs_window_create_response *response)
{
    if (title == NULL) {
        return openrfs_result(-OPENRFS_EFAULT);
    }
    const struct openrfs_window_create_request request = {
        sizeof(request), OPENRFS_ABI_VERSION, (uint64_t)(uintptr_t)title,
        (uint32_t)strlen(title), width, height, OPENRFS_PIXEL_XRGB8888, 0U, 0U
    };
    return openrfs_result(openrfs_syscall2(OPENRFS_SYS_WINDOW_CREATE,
        (uint64_t)(uintptr_t)&request, (uint64_t)(uintptr_t)response));
}
long openrfs_surface_present(openrfs_handle_t window,
    const struct openrfs_rect *rectangles, size_t count)
{
    const struct openrfs_present_request request = {sizeof(request),
        OPENRFS_ABI_VERSION, window, (uint64_t)(uintptr_t)rectangles,
        (uint32_t)count, 0U};
    return openrfs_syscall1(OPENRFS_SYS_SURFACE_PRESENT,
        (uint64_t)(uintptr_t)&request);
}
long openrfs_event_read(openrfs_handle_t events, struct openrfs_event *event)
{ return openrfs_syscall2(OPENRFS_SYS_EVENT_READ, events, (uint64_t)(uintptr_t)event); }
long openrfs_event_wait(openrfs_handle_t events, uint64_t deadline_ns)
{
    struct openrfs_wait_item item = {events, OPENRFS_WAIT_READABLE, 0U};
    const struct openrfs_wait_request request = {sizeof(request),
        OPENRFS_ABI_VERSION, (uint64_t)(uintptr_t)&item, deadline_ns, 1U, 0U};
    return openrfs_syscall1(OPENRFS_SYS_WAIT, (uint64_t)(uintptr_t)&request);
}
long openrfs_pointer_capture(openrfs_handle_t window, int capture)
{ return openrfs_syscall2(OPENRFS_SYS_POINTER_CAPTURE, window, capture != 0); }

long openrfs_dns_resolve(const char *hostname, uint64_t deadline_ns)
{
    if (hostname == NULL) return -OPENRFS_EFAULT;
    return openrfs_syscall3(OPENRFS_SYS_DNS_RESOLVE,
        (uint64_t)(uintptr_t)hostname, strlen(hostname), deadline_ns);
}
long openrfs_stream_open(void) { return openrfs_syscall0(OPENRFS_SYS_STREAM_OPEN); }
long openrfs_stream_connect(openrfs_handle_t stream,
    const struct openrfs_ipv4_endpoint *endpoint, uint64_t deadline_ns)
{ return openrfs_syscall3(OPENRFS_SYS_STREAM_CONNECT, stream, (uint64_t)(uintptr_t)endpoint, deadline_ns); }
static long network_io(uint64_t number, openrfs_handle_t handle, void *buffer,
    size_t length, uint64_t deadline, struct openrfs_ipv4_endpoint *endpoint)
{
    struct openrfs_network_io request;

    if (length == 0U || length > UINT32_MAX) {
        return -OPENRFS_EINVAL;
    }
    if ((number == OPENRFS_SYS_STREAM_READ ||
            number == OPENRFS_SYS_STREAM_WRITE) &&
        length > OPENRFS_NETWORK_IO_MAX_BYTES) {
        length = OPENRFS_NETWORK_IO_MAX_BYTES;
    }
    request = (struct openrfs_network_io){sizeof(request), OPENRFS_ABI_VERSION,
        handle, (uint64_t)(uintptr_t)buffer, deadline, {0U, 0U, 0U},
        (uint32_t)length, 0U};
    if (endpoint != NULL) request.endpoint = *endpoint;
    const long result = openrfs_syscall1(number, (uint64_t)(uintptr_t)&request);
    if (result >= 0 && endpoint != NULL &&
        number == OPENRFS_SYS_DATAGRAM_RECEIVE) *endpoint = request.endpoint;
    return result;
}
long openrfs_stream_read(openrfs_handle_t stream, void *buffer, size_t length,
    uint64_t deadline_ns)
{ return network_io(OPENRFS_SYS_STREAM_READ, stream, buffer, length, deadline_ns, NULL); }
long openrfs_stream_write(openrfs_handle_t stream, const void *buffer,
    size_t length, uint64_t deadline_ns)
{ return network_io(OPENRFS_SYS_STREAM_WRITE, stream, (void *)(uintptr_t)buffer, length, deadline_ns, NULL); }
long openrfs_stream_shutdown(openrfs_handle_t stream, uint32_t flags,
    uint64_t deadline_ns)
{ return openrfs_syscall3(OPENRFS_SYS_STREAM_SHUTDOWN, stream, flags, deadline_ns); }
long openrfs_datagram_open(void) { return openrfs_syscall0(OPENRFS_SYS_DATAGRAM_OPEN); }
long openrfs_datagram_bind(openrfs_handle_t datagram, uint16_t port)
{ return openrfs_syscall2(OPENRFS_SYS_DATAGRAM_BIND, datagram, port); }
long openrfs_datagram_send(openrfs_handle_t datagram,
    const struct openrfs_ipv4_endpoint *destination, const void *buffer,
    size_t length, uint64_t deadline_ns)
{
    if (destination == NULL) return -OPENRFS_EFAULT;
    struct openrfs_ipv4_endpoint endpoint = *destination;
    return network_io(OPENRFS_SYS_DATAGRAM_SEND, datagram,
        (void *)(uintptr_t)buffer, length, deadline_ns, &endpoint);
}
long openrfs_datagram_receive(openrfs_handle_t datagram,
    struct openrfs_ipv4_endpoint *source, void *buffer, size_t length,
    uint64_t deadline_ns)
{ return network_io(OPENRFS_SYS_DATAGRAM_RECEIVE, datagram, buffer, length, deadline_ns, source); }
long openrfs_network_address(openrfs_handle_t handle, int peer,
    struct openrfs_ipv4_endpoint *endpoint)
{ return openrfs_syscall3(OPENRFS_SYS_NETWORK_ADDRESS, handle, peer != 0, (uint64_t)(uintptr_t)endpoint); }
long openrfs_network_cancel(openrfs_handle_t handle)
{ return openrfs_syscall1(OPENRFS_SYS_CANCEL, handle); }
