/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Network devices for iPXE drivers.
 *
 * The structure layout and function contracts follow iPXE's
 * include/ipxe/netdevice.h and net/netdevice.c (GPL-2.0-or-later OR UBDL):
 * a driver fills in hardware and link-layer addresses, supplies
 * open/close/transmit/poll/irq operations, and reports completions with
 * netdev_tx_complete*() and receptions with netdev_rx*(). The queues,
 * statistics and state bits behave as upstream describes them. What is not
 * here is iPXE's upper stack: received buffers go to OpenRFS's IPv4 stack
 * through ports/ipxe/ipxe_glue.c instead of to iPXE's protocols.
 */
#ifndef OPENRFS_IPXE_NETDEVICE_H
#define OPENRFS_IPXE_NETDEVICE_H

#include <stddef.h>
#include <stdint.h>
#include <ipxe/device.h>
#include <ipxe/dma.h>
#include <ipxe/list.h>
#include <ipxe/refcnt.h>
#include <ipxe/settings.h>

struct io_buffer;
struct net_device;

#define MAX_HW_ADDR_LEN 8
#define MAX_LL_ADDR_LEN 20
#define MAX_LL_HEADER_LEN 64
#define MAX_NET_ADDR_LEN 16
#define NETDEV_NAME_LEN 12
#define NETDEV_MAX_UNIQUE_ERRORS 4

#define NETDEV_OPEN 0x0001
#define NETDEV_IRQ_ENABLED 0x0002
#define NETDEV_RX_FROZEN 0x0004
#define NETDEV_IRQ_UNSUPPORTED 0x0008
#define NETDEV_TX_IN_PROGRESS 0x0010
#define NETDEV_POLL_IN_PROGRESS 0x0020
#define NETDEV_INSOMNIAC 0x0040

#define LL_MULTICAST 0x0001
#define LL_BROADCAST 0x0002

struct ll_protocol {
    const char *name;
    int (*push)(struct net_device *netdev, struct io_buffer *iobuf,
        const void *ll_dest, const void *ll_source, uint16_t net_proto);
    int (*pull)(struct net_device *netdev, struct io_buffer *iobuf,
        const void **ll_dest, const void **ll_source, uint16_t *net_proto,
        unsigned int *flags);
    void (*init_addr)(const void *hw_addr, void *ll_addr);
    const char *(*ntoa)(const void *ll_addr);
    int (*mc_hash)(unsigned int af, const void *net_addr, void *ll_addr);
    int (*eth_addr)(const void *ll_addr, void *eth_addr);
    int (*eui64)(const void *ll_addr, void *eui64);
    uint16_t ll_proto;
    uint8_t hw_addr_len;
    uint8_t ll_addr_len;
    uint8_t ll_header_len;
    unsigned int flags;
};

#define __ll_protocol

struct net_device_operations {
    int (*open)(struct net_device *netdev);
    void (*close)(struct net_device *netdev);
    int (*transmit)(struct net_device *netdev, struct io_buffer *iobuf);
    void (*poll)(struct net_device *netdev);
    void (*irq)(struct net_device *netdev, int enable);
};

struct net_device_error {
    int rc;
    unsigned int count;
};

struct net_device_stats {
    unsigned int good;
    unsigned int bad;
    struct net_device_error errors[NETDEV_MAX_UNIQUE_ERRORS];
};

struct net_device {
    struct refcnt refcnt;
    struct list_head list;
    struct list_head open_list;
    unsigned int scope_id;
    char name[NETDEV_NAME_LEN];
    struct device *dev;
    struct dma_device *dma;
    struct net_device_operations *op;
    struct ll_protocol *ll_protocol;
    uint8_t hw_addr[MAX_HW_ADDR_LEN];
    uint8_t ll_addr[MAX_LL_ADDR_LEN];
    const uint8_t *ll_broadcast;
    unsigned int state;
    int link_rc;
    size_t max_pkt_len;
    size_t mtu;
    struct list_head tx_queue;
    struct list_head tx_deferred;
    struct list_head rx_queue;
    struct net_device_stats tx_stats;
    struct net_device_stats rx_stats;
    struct generic_settings settings;
    void *priv;
    /* OpenRFS: the glue record that owns this device. */
    void *openrfs_owner;
    size_t openrfs_rx_queued;
};

extern struct net_device_operations null_netdev_operations;

static inline void netdev_init(struct net_device *netdev,
    struct net_device_operations *op)
{
    netdev->op = op;
}

static inline void netdev_nullify(struct net_device *netdev)
{
    netdev->op = &null_netdev_operations;
}

static inline struct net_device *netdev_get(struct net_device *netdev)
{
    ref_get(&netdev->refcnt);
    return netdev;
}

static inline void netdev_put(struct net_device *netdev)
{
    ref_put(&netdev->refcnt);
}

static inline struct settings *netdev_settings(struct net_device *netdev)
{
    return &netdev->settings.settings;
}

static inline int netdev_is_open(struct net_device *netdev)
{
    return (netdev->state & NETDEV_OPEN) != 0;
}

static inline int netdev_irq_supported(struct net_device *netdev)
{
    return (netdev->state & NETDEV_IRQ_UNSUPPORTED) == 0 &&
        netdev->op->irq != 0;
}

static inline int netdev_irq_enabled(struct net_device *netdev)
{
    return (netdev->state & NETDEV_IRQ_ENABLED) != 0;
}

static inline int netdev_rx_frozen(struct net_device *netdev)
{
    return (netdev->state & NETDEV_RX_FROZEN) != 0;
}

static inline int netdev_link_ok(struct net_device *netdev)
{
    return netdev->link_rc == 0;
}

const char *netdev_addr(struct net_device *netdev);
void netdev_link_err(struct net_device *netdev, int rc);
void netdev_link_down(struct net_device *netdev);

static inline void netdev_link_up(struct net_device *netdev)
{
    netdev_link_err(netdev, 0);
}

struct net_device *alloc_netdev(size_t priv_len);
int register_netdev(struct net_device *netdev);
void unregister_netdev(struct net_device *netdev);
int netdev_open(struct net_device *netdev);
void netdev_close(struct net_device *netdev);
void netdev_irq(struct net_device *netdev, int enable);
int netdev_tx(struct net_device *netdev, struct io_buffer *iobuf);
void netdev_tx_defer(struct net_device *netdev, struct io_buffer *iobuf);
void netdev_tx_err(struct net_device *netdev, struct io_buffer *iobuf,
    int rc);
void netdev_tx_complete_err(struct net_device *netdev,
    struct io_buffer *iobuf, int rc);
void netdev_tx_complete_next_err(struct net_device *netdev, int rc);
void netdev_rx(struct net_device *netdev, struct io_buffer *iobuf);
void netdev_rx_err(struct net_device *netdev, struct io_buffer *iobuf,
    int rc);
void netdev_poll(struct net_device *netdev);
struct io_buffer *netdev_rx_dequeue(struct net_device *netdev);

static inline void netdev_tx_complete(struct net_device *netdev,
    struct io_buffer *iobuf)
{
    netdev_tx_complete_err(netdev, iobuf, 0);
}

static inline void netdev_tx_complete_next(struct net_device *netdev)
{
    netdev_tx_complete_next_err(netdev, 0);
}

#endif
