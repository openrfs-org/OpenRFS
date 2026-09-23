/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_NETDEV_H
#define OPENRFS_NETDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/virtio_net.h>

/*
 * The network interface the stack talks to.
 *
 * The IPv4 stack in network.c was written against one controller, virtio-net,
 * and it still speaks that driver's status vocabulary and device state. This
 * layer keeps that contract and puts more than one kind of controller behind
 * it: virtio-net remains the first choice exactly as before, and when no
 * virtio-net function is present the first interface a bound upstream driver
 * registered becomes the active one.
 *
 * A registered interface lends the layer an operations table and a context.
 * Every operation is polled; none may block beyond its own bounded device
 * waits, because the stack calls them from its service loop.
 */
#define NETDEV_MAX_INTERFACES 8U
#define NETDEV_NAME_CAPACITY 16U
#define NETDEV_DRIVER_CAPACITY 24U

struct netdev_operations {
    enum virtio_net_status (*service)(void *context);
    enum virtio_net_status (*reset)(void *context);
    enum virtio_net_status (*transmit)(void *context, const uint8_t *frame,
        size_t length);
    enum virtio_net_status (*receive)(void *context, uint8_t *frame,
        size_t capacity, size_t *length);
    struct virtio_net_state (*state)(void *context);
    enum virtio_net_status (*shutdown)(void *context);
};

enum netdev_kind {
    NETDEV_KIND_NONE = 0,
    NETDEV_KIND_VIRTIO,
    NETDEV_KIND_REGISTERED
};

struct netdev_interface_info {
    char name[NETDEV_NAME_CAPACITY];
    char driver[NETDEV_DRIVER_CAPACITY];
    uint8_t mac[6];
    bool link_up;
    bool active;
};

/* Upstream drivers register interfaces before the network foundation runs. */
enum virtio_net_status netdev_register(
    const char *driver,
    const struct netdev_operations *operations,
    void *context,
    size_t *index
);
size_t netdev_interface_count(void);
bool netdev_interface_info(size_t index, struct netdev_interface_info *info);

/* The virtio-net-shaped contract network.c calls. */
enum virtio_net_status netdev_initialize(void);
enum virtio_net_status netdev_shutdown(void);
enum virtio_net_status netdev_reset(void);
enum virtio_net_status netdev_service(void);
enum virtio_net_status netdev_transmit(const uint8_t *frame, size_t length);
enum virtio_net_status netdev_receive(
    uint8_t *frame,
    size_t capacity,
    size_t *length
);
struct virtio_net_state netdev_get_state(void);

enum netdev_kind netdev_active_kind(void);
/* "virtio-net0" for the native driver, otherwise the registered name. */
const char *netdev_active_name(void);
const char *netdev_active_driver(void);
/* The registered interface whose initialization just failed, or NULL. */
const char *netdev_failed_name(void);
/*
 * Make a registered interface the active one. Only valid while the stack is
 * not initialized, so no endpoint can straddle two controllers.
 */
enum virtio_net_status netdev_select(size_t index);

#endif
