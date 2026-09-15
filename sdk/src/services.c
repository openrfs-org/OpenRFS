/* SPDX-License-Identifier: GPL-3.0-only */
#include <opengat/event.h>
#include <opengat/network.h>
#include <opengat/runtime.h>
#include <opengat/window.h>

#include <errno.h>
#include <string.h>

long opengat_wait(struct opengat_wait_item *items, size_t count,
    uint64_t deadline_ns)
{
    const struct opengat_wait_request request = {sizeof(request),
        OPENGAT_ABI_VERSION, (uint64_t)(uintptr_t)items, deadline_ns,
        (uint32_t)count, 0U};

    if (items == NULL || count == 0U || count > OPENGAT_WAIT_MAX) {
        return -OPENGAT_EINVAL;
    }
    return opengat_syscall1(OPENGAT_SYS_WAIT,
        (uint64_t)(uintptr_t)&request);
}

long opengat_timer_create(void)
{
    return opengat_syscall0(OPENGAT_SYS_TIMER_CREATE);
}

long opengat_timer_set(opengat_handle_t timer, uint64_t deadline_ns)
{
    const struct opengat_timer_set_request request = {sizeof(request),
        OPENGAT_ABI_VERSION, timer, deadline_ns, 0U, 0U};

    return opengat_syscall1(OPENGAT_SYS_TIMER_SET,
        (uint64_t)(uintptr_t)&request);
}

long opengat_cancel(opengat_handle_t handle)
{
    return opengat_syscall1(OPENGAT_SYS_CANCEL, handle);
}

int opengat_window_create(const char *title, uint32_t width, uint32_t height,
    struct opengat_window_create_response *response)
{
    if (title == NULL) {
        return opengat_result(-OPENGAT_EFAULT);
    }
    const struct opengat_window_create_request request = {
        sizeof(request), OPENGAT_ABI_VERSION, (uint64_t)(uintptr_t)title,
        (uint32_t)strlen(title), width, height, OPENGAT_PIXEL_XRGB8888, 0U, 0U
    };
    return opengat_result(opengat_syscall2(OPENGAT_SYS_WINDOW_CREATE,
        (uint64_t)(uintptr_t)&request, (uint64_t)(uintptr_t)response));
}
long opengat_surface_present(opengat_handle_t window,
    const struct opengat_rect *rectangles, size_t count)
{
    const struct opengat_present_request request = {sizeof(request),
        OPENGAT_ABI_VERSION, window, (uint64_t)(uintptr_t)rectangles,
        (uint32_t)count, 0U};
    return opengat_syscall1(OPENGAT_SYS_SURFACE_PRESENT,
        (uint64_t)(uintptr_t)&request);
}
long opengat_event_read(opengat_handle_t events, struct opengat_event *event)
{ return opengat_syscall2(OPENGAT_SYS_EVENT_READ, events, (uint64_t)(uintptr_t)event); }
long opengat_event_wait(opengat_handle_t events, uint64_t deadline_ns)
{
    struct opengat_wait_item item = {events, OPENGAT_WAIT_READABLE, 0U};
    const struct opengat_wait_request request = {sizeof(request),
        OPENGAT_ABI_VERSION, (uint64_t)(uintptr_t)&item, deadline_ns, 1U, 0U};
    return opengat_syscall1(OPENGAT_SYS_WAIT, (uint64_t)(uintptr_t)&request);
}
long opengat_pointer_capture(opengat_handle_t window, int capture)
{ return opengat_syscall2(OPENGAT_SYS_POINTER_CAPTURE, window, capture != 0); }

long opengat_dns_resolve(const char *hostname, uint64_t deadline_ns)
{
    if (hostname == NULL) return -OPENGAT_EFAULT;
    return opengat_syscall3(OPENGAT_SYS_DNS_RESOLVE,
        (uint64_t)(uintptr_t)hostname, strlen(hostname), deadline_ns);
}
long opengat_stream_open(void) { return opengat_syscall0(OPENGAT_SYS_STREAM_OPEN); }
long opengat_stream_connect(opengat_handle_t stream,
    const struct opengat_ipv4_endpoint *endpoint, uint64_t deadline_ns)
{ return opengat_syscall3(OPENGAT_SYS_STREAM_CONNECT, stream, (uint64_t)(uintptr_t)endpoint, deadline_ns); }
static long network_io(uint64_t number, opengat_handle_t handle, void *buffer,
    size_t length, uint64_t deadline, struct opengat_ipv4_endpoint *endpoint)
{
    struct opengat_network_io request;

    if (length == 0U || length > UINT32_MAX) {
        return -OPENGAT_EINVAL;
    }
    if ((number == OPENGAT_SYS_STREAM_READ ||
            number == OPENGAT_SYS_STREAM_WRITE) &&
        length > OPENGAT_NETWORK_IO_MAX_BYTES) {
        length = OPENGAT_NETWORK_IO_MAX_BYTES;
    }
    request = (struct opengat_network_io){sizeof(request), OPENGAT_ABI_VERSION,
        handle, (uint64_t)(uintptr_t)buffer, deadline, {0U, 0U, 0U},
        (uint32_t)length, 0U};
    if (endpoint != NULL) request.endpoint = *endpoint;
    const long result = opengat_syscall1(number, (uint64_t)(uintptr_t)&request);
    if (result >= 0 && endpoint != NULL &&
        number == OPENGAT_SYS_DATAGRAM_RECEIVE) *endpoint = request.endpoint;
    return result;
}
long opengat_stream_read(opengat_handle_t stream, void *buffer, size_t length,
    uint64_t deadline_ns)
{ return network_io(OPENGAT_SYS_STREAM_READ, stream, buffer, length, deadline_ns, NULL); }
long opengat_stream_write(opengat_handle_t stream, const void *buffer,
    size_t length, uint64_t deadline_ns)
{ return network_io(OPENGAT_SYS_STREAM_WRITE, stream, (void *)(uintptr_t)buffer, length, deadline_ns, NULL); }
long opengat_stream_shutdown(opengat_handle_t stream, uint32_t flags,
    uint64_t deadline_ns)
{ return opengat_syscall3(OPENGAT_SYS_STREAM_SHUTDOWN, stream, flags, deadline_ns); }
long opengat_datagram_open(void) { return opengat_syscall0(OPENGAT_SYS_DATAGRAM_OPEN); }
long opengat_datagram_bind(opengat_handle_t datagram, uint16_t port)
{ return opengat_syscall2(OPENGAT_SYS_DATAGRAM_BIND, datagram, port); }
long opengat_datagram_send(opengat_handle_t datagram,
    const struct opengat_ipv4_endpoint *destination, const void *buffer,
    size_t length, uint64_t deadline_ns)
{
    if (destination == NULL) return -OPENGAT_EFAULT;
    struct opengat_ipv4_endpoint endpoint = *destination;
    return network_io(OPENGAT_SYS_DATAGRAM_SEND, datagram,
        (void *)(uintptr_t)buffer, length, deadline_ns, &endpoint);
}
long opengat_datagram_receive(opengat_handle_t datagram,
    struct opengat_ipv4_endpoint *source, void *buffer, size_t length,
    uint64_t deadline_ns)
{ return network_io(OPENGAT_SYS_DATAGRAM_RECEIVE, datagram, buffer, length, deadline_ns, source); }
long opengat_network_address(opengat_handle_t handle, int peer,
    struct opengat_ipv4_endpoint *endpoint)
{ return opengat_syscall3(OPENGAT_SYS_NETWORK_ADDRESS, handle, peer != 0, (uint64_t)(uintptr_t)endpoint); }
long opengat_network_cancel(opengat_handle_t handle)
{ return opengat_syscall1(OPENGAT_SYS_CANCEL, handle); }
