/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Dispatch from the IPv4 stack to whichever controller is active. virtio-net
 * keeps its place as the first choice so every existing scenario sees the
 * same device, the same messages and the same status codes it always did.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/netdev.h>
#include <openrfs/virtio_net.h>

struct netdev_interface {
    char name[NETDEV_NAME_CAPACITY];
    char driver[NETDEV_DRIVER_CAPACITY];
    const struct netdev_operations *operations;
    void *context;
    bool registered;
};

static struct netdev_interface interfaces[NETDEV_MAX_INTERFACES];
static size_t interface_count;
static size_t preferred_index;
static size_t active_index;
static enum netdev_kind active_kind;
static size_t failed_index = SIZE_MAX;
static bool stack_initialized;

static void copy_text(char *destination, const char *source, size_t capacity)
{
    size_t index = 0U;

    while (source != NULL && index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static void format_name(char *destination, size_t index)
{
    destination[0] = 'n';
    destination[1] = 'e';
    destination[2] = 't';
    destination[3] = (char)('0' + (char)(index % 10U));
    destination[4] = '\0';
}

enum virtio_net_status netdev_register(
    const char *driver,
    const struct netdev_operations *operations,
    void *context,
    size_t *index
)
{
    struct netdev_interface *interface;

    if (driver == NULL || operations == NULL || operations->service == NULL ||
        operations->transmit == NULL || operations->receive == NULL ||
        operations->state == NULL) {
        return VIRTIO_NET_STATUS_NULL_ARGUMENT;
    }
    if (interface_count >= NETDEV_MAX_INTERFACES) {
        return VIRTIO_NET_STATUS_TX_EXHAUSTED;
    }
    interface = &interfaces[interface_count];
    format_name(interface->name, interface_count);
    copy_text(interface->driver, driver, sizeof(interface->driver));
    interface->operations = operations;
    interface->context = context;
    interface->registered = true;
    if (index != NULL) {
        *index = interface_count;
    }
    ++interface_count;
    return VIRTIO_NET_STATUS_OK;
}

size_t netdev_interface_count(void)
{
    return interface_count;
}

bool netdev_interface_info(size_t index, struct netdev_interface_info *info)
{
    struct virtio_net_state state;

    if (info == NULL || index >= interface_count) {
        return false;
    }
    state = interfaces[index].operations->state(interfaces[index].context);
    copy_text(info->name, interfaces[index].name, sizeof(info->name));
    copy_text(info->driver, interfaces[index].driver, sizeof(info->driver));
    for (size_t byte = 0U; byte < sizeof(info->mac); ++byte) {
        info->mac[byte] = state.mac[byte];
    }
    info->link_up = state.link_up;
    info->active = active_kind == NETDEV_KIND_REGISTERED &&
        active_index == index;
    return true;
}

static struct netdev_interface *active_interface(void)
{
    if (active_kind != NETDEV_KIND_REGISTERED ||
        active_index >= interface_count) {
        return NULL;
    }
    return &interfaces[active_index];
}

enum virtio_net_status netdev_initialize(void)
{
    enum virtio_net_status status;

    failed_index = SIZE_MAX;
    status = virtio_net_initialize();
    struct netdev_interface *interface;
    struct virtio_net_state state;

    if (status != VIRTIO_NET_STATUS_ABSENT) {
        active_kind = NETDEV_KIND_VIRTIO;
        stack_initialized = status == VIRTIO_NET_STATUS_OK ||
            status == VIRTIO_NET_STATUS_LINK_DOWN;
        return status;
    }
    if (interface_count == 0U) {
        active_kind = NETDEV_KIND_NONE;
        return status;
    }
    active_index = preferred_index < interface_count ? preferred_index : 0U;
    active_kind = NETDEV_KIND_REGISTERED;
    interface = active_interface();
    if (interface->operations->reset != NULL) {
        status = interface->operations->reset(interface->context);
        if (status != VIRTIO_NET_STATUS_OK &&
            status != VIRTIO_NET_STATUS_LINK_DOWN) {
            /* Keep naming the interface that refused for the caller. */
            failed_index = active_index;
            active_kind = NETDEV_KIND_NONE;
            return status;
        }
    }
    state = interface->operations->state(interface->context);
    stack_initialized = true;
    return state.link_up ? VIRTIO_NET_STATUS_OK : VIRTIO_NET_STATUS_LINK_DOWN;
}

enum virtio_net_status netdev_shutdown(void)
{
    struct netdev_interface *interface = active_interface();
    enum virtio_net_status status = VIRTIO_NET_STATUS_OK;

    stack_initialized = false;
    if (interface == NULL) {
        return virtio_net_shutdown();
    }
    if (interface->operations->shutdown != NULL) {
        status = interface->operations->shutdown(interface->context);
    }
    return status;
}

enum virtio_net_status netdev_reset(void)
{
    struct netdev_interface *interface = active_interface();

    if (interface == NULL) {
        return virtio_net_reset();
    }
    if (interface->operations->reset == NULL) {
        return VIRTIO_NET_STATUS_OK;
    }
    return interface->operations->reset(interface->context);
}

enum virtio_net_status netdev_service(void)
{
    struct netdev_interface *interface = active_interface();

    if (interface == NULL) {
        return virtio_net_service();
    }
    return interface->operations->service(interface->context);
}

enum virtio_net_status netdev_transmit(const uint8_t *frame, size_t length)
{
    struct netdev_interface *interface = active_interface();

    if (interface == NULL) {
        return virtio_net_transmit(frame, length);
    }
    if (frame == NULL) {
        return VIRTIO_NET_STATUS_NULL_ARGUMENT;
    }
    if (length > VIRTIO_NET_MAX_FRAME_SIZE) {
        return VIRTIO_NET_STATUS_FRAME_TOO_LARGE;
    }
    return interface->operations->transmit(interface->context, frame,
        length);
}

enum virtio_net_status netdev_receive(
    uint8_t *frame,
    size_t capacity,
    size_t *length
)
{
    struct netdev_interface *interface = active_interface();

    if (interface == NULL) {
        return virtio_net_receive(frame, capacity, length);
    }
    if (frame == NULL || length == NULL) {
        return VIRTIO_NET_STATUS_NULL_ARGUMENT;
    }
    return interface->operations->receive(interface->context, frame,
        capacity, length);
}

struct virtio_net_state netdev_get_state(void)
{
    struct netdev_interface *interface = active_interface();

    if (interface == NULL) {
        return virtio_net_get_state();
    }
    return interface->operations->state(interface->context);
}

enum netdev_kind netdev_active_kind(void)
{
    return active_kind;
}

const char *netdev_active_name(void)
{
    struct netdev_interface *interface = active_interface();

    return interface == NULL ? "virtio-net0" : interface->name;
}

const char *netdev_failed_name(void)
{
    return failed_index < interface_count ? interfaces[failed_index].name :
        NULL;
}

const char *netdev_active_driver(void)
{
    struct netdev_interface *interface = active_interface();

    return interface == NULL ? "virtio-net" : interface->driver;
}

enum virtio_net_status netdev_select(size_t index)
{
    if (index >= interface_count) {
        return VIRTIO_NET_STATUS_ABSENT;
    }
    if (stack_initialized) {
        return VIRTIO_NET_STATUS_ALREADY_INITIALIZED;
    }
    preferred_index = index;
    return VIRTIO_NET_STATUS_OK;
}
