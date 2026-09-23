/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The iPXE-facing half of the OpenRFS iPXE compatibility layer.
 *
 * This file gives the vendored drivers the runtime they expect - memory, DMA
 * mappings, delays, PCI access and the net_device core - and translates
 * between that runtime and the kernel through include/openrfs/ipxe_host.h.
 *
 * The net_device functions below reproduce the behaviour of iPXE's
 * net/netdevice.c (GPL-2.0-or-later OR UBDL) for the paths a driver can
 * reach: transmit queueing and completion, deferred transmits, receive
 * queueing, link status, open/close and interrupt control. The receive queue
 * is additionally bounded, because OpenRFS drains it from its own service
 * loop rather than from iPXE's network stack.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <byteswap.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ipxe/dma.h>
#include <ipxe/ethernet.h>
#include <ipxe/if_ether.h>
#include <ipxe/iobuf.h>
#include <ipxe/malloc.h>
#include <ipxe/netdevice.h>
#include <ipxe/pci.h>
#include <ipxe/timer.h>
#include <nic.h>

#include <openrfs/ipxe_host.h>

#define IPXE_GLUE_MAX_DEVICES 8U
#define IPXE_GLUE_RX_QUEUE_LIMIT 64U
#define IPXE_GLUE_MINIMUM_ALIGNMENT 16U
#define IPXE_GLUE_CAPABILITY_LIMIT 48U
/* Link status codes, as iPXE keeps them in net_device.link_rc. */
#define GLUE_LINK_RC_UNKNOWN (-EINPROGRESS)
#define GLUE_LINK_RC_DOWN (-ENOTCONN)

struct glue_driver {
    const char *name;
    const char *label;
    const char *path;
    struct pci_driver *driver;
};

struct openrfs_ipxe_pci {
    struct pci_device pci;
    const struct glue_driver *entry;
    void *handle;
    struct net_device *netdev;
    size_t function_index;
    uint64_t polls;
    char instance[IPXE_HOST_INSTANCE_CAPACITY];
    bool bound;
};

extern struct pci_driver intel_driver;
extern struct pci_driver ifec_driver;
extern struct pci_driver realtek_driver;
extern struct pci_driver pcnet32_driver;
extern struct pci_driver vmxnet3_driver;
extern struct pci_driver tulip_driver;
extern struct pci_driver nepci_driver;

static const struct glue_driver glue_drivers[] = {
    { "intel", "Intel PRO/1000", "src/drivers/net/intel.c",
        &intel_driver },
    { "eepro100", "Intel PRO/100", "src/drivers/net/eepro100.c",
        &ifec_driver },
    { "realtek", "Realtek RTL8139/RTL8169", "src/drivers/net/realtek.c",
        &realtek_driver },
    { "pcnet32", "AMD PCnet", "src/drivers/net/pcnet32.c",
        &pcnet32_driver },
    { "vmxnet3", "VMware VMXNET3", "src/drivers/net/vmxnet3.c",
        &vmxnet3_driver },
    { "tulip", "DEC/Intel Tulip", "src/drivers/net/tulip.c",
        &tulip_driver },
    { "ne2k-pci", "NE2000 PCI", "src/drivers/net/ns8390.c",
        &nepci_driver }
};

#define GLUE_DRIVER_COUNT (sizeof(glue_drivers) / sizeof(glue_drivers[0]))

static struct openrfs_ipxe_pci devices[IPXE_GLUE_MAX_DEVICES];
/* The device whose driver is running; ioremap() and register_netdev() use it. */
static struct openrfs_ipxe_pci *current_device;

struct net_device_operations null_netdev_operations;

/* Memory. */

static size_t power_of_two_at_least(size_t value)
{
    size_t result = IPXE_GLUE_MINIMUM_ALIGNMENT;

    while (result < value && result <= (SIZE_MAX >> 1)) {
        result <<= 1;
    }
    return result;
}

void *malloc(size_t size)
{
    return ipxe_host_alloc(size != 0U ? size : 1U,
        IPXE_GLUE_MINIMUM_ALIGNMENT);
}

void *zalloc(size_t size)
{
    /* The arena zeroes every block it hands out. */
    return malloc(size);
}

void free(void *pointer)
{
    if (pointer != NULL) {
        ipxe_host_free(pointer);
    }
}

void zfree(void *pointer)
{
    if (pointer != NULL) {
        memset(pointer, 0, ipxe_host_allocation_size(pointer));
        free(pointer);
    }
}

void *realloc(void *old_pointer, size_t new_size)
{
    void *new_pointer;
    size_t old_size;

    if (old_pointer == NULL) {
        return malloc(new_size);
    }
    if (new_size == 0U) {
        free(old_pointer);
        return NULL;
    }
    new_pointer = malloc(new_size);
    if (new_pointer == NULL) {
        return NULL;
    }
    old_size = ipxe_host_allocation_size(old_pointer);
    memcpy(new_pointer, old_pointer, old_size < new_size ? old_size :
        new_size);
    free(old_pointer);
    return new_pointer;
}

void *malloc_phys_offset(size_t size, size_t physical_align, size_t offset)
{
    const size_t align = power_of_two_at_least(physical_align);

    /*
     * Every vendored caller asks for offset zero. A non-zero offset would
     * need the returned pointer to sit inside its block, which free_phys()
     * could not then name, so it is refused rather than approximated.
     */
    if ((offset & (align - 1U)) != 0U) {
        return NULL;
    }
    return ipxe_host_alloc(size != 0U ? size : 1U, align);
}

void *malloc_phys(size_t size, size_t physical_align)
{
    return malloc_phys_offset(size, physical_align, 0U);
}

void free_phys(void *pointer, size_t size)
{
    (void)size;
    free(pointer);
}

/* DMA. */

int dma_map(struct dma_device *dma, struct dma_mapping *map, void *address,
    size_t length, int flags)
{
    (void)flags;
    if (!ipxe_host_arena_contains(address, length != 0U ? length : 1U)) {
        return -ENOTSUP;
    }
    map->dma = dma;
    map->offset = 0U;
    map->token = NULL;
    if (dma != NULL) {
        ++dma->mapped;
    }
    return 0;
}

void dma_unmap(struct dma_mapping *map, size_t length)
{
    (void)length;
    if (map->dma != NULL && map->dma->mapped > 0U) {
        --map->dma->mapped;
    }
    map->dma = NULL;
}

void *dma_alloc(struct dma_device *dma, struct dma_mapping *map,
    size_t length, size_t align)
{
    void *address = malloc_phys(length, align);

    if (address != NULL) {
        map->dma = dma;
        map->offset = 0U;
        map->token = NULL;
        if (dma != NULL) {
            ++dma->allocated;
        }
    }
    return address;
}

void dma_free(struct dma_mapping *map, void *address, size_t length)
{
    free_phys(address, length);
    if (map->dma != NULL && map->dma->allocated > 0U) {
        --map->dma->allocated;
    }
    map->dma = NULL;
}

void *dma_umalloc(struct dma_device *dma, struct dma_mapping *map,
    size_t length, size_t align)
{
    return dma_alloc(dma, map, length, align);
}

void dma_ufree(struct dma_mapping *map, void *address, size_t length)
{
    dma_free(map, address, length);
}

void dma_set_mask(struct dma_device *dma, physaddr_t mask)
{
    /* The arena lies below 4 GiB, inside every mask a driver can set. */
    dma->mask = mask;
}

/* Time. */

void udelay(unsigned long usecs)
{
    ipxe_host_delay_us(usecs);
}

void mdelay(unsigned long msecs)
{
    ipxe_host_delay_us(msecs * 1000UL);
}

unsigned long currticks(void)
{
    return ipxe_host_ticks_ms();
}

unsigned int sleep(unsigned int seconds)
{
    mdelay((unsigned long)seconds * 1000UL);
    return 0U;
}

void sleep_fixed(unsigned int seconds)
{
    (void)sleep(seconds);
}

/* Device windows. */

void *pci_ioremap(struct pci_device *pci, unsigned long bus_addr, size_t len)
{
    if (pci == NULL || pci->openrfs == NULL) {
        return NULL;
    }
    return ipxe_host_map(pci->openrfs->handle, bus_addr, len);
}

void *ioremap(unsigned long bus_address, size_t length)
{
    if (current_device == NULL) {
        return NULL;
    }
    return ipxe_host_map(current_device->handle, bus_address, length);
}

void iounmap(volatile const void *io_address)
{
    /* Windows belong to the claim and are released with it. */
    (void)io_address;
}

/* PCI configuration. */

static bool bar_for_register(struct pci_device *pci, unsigned int reg,
    struct ipxe_host_bar *bar)
{
    if (pci == NULL || pci->openrfs == NULL || reg < PCI_BASE_ADDRESS_0 ||
        reg > PCI_BASE_ADDRESS_5 || ((reg - PCI_BASE_ADDRESS_0) & 3U) != 0U) {
        return false;
    }
    return ipxe_host_bar(pci->openrfs->handle,
        (reg - PCI_BASE_ADDRESS_0) / 4U, bar);
}

int pci_bar_is_io(struct pci_device *pci, unsigned int reg)
{
    struct ipxe_host_bar bar;

    return bar_for_register(pci, reg, &bar) && bar.implemented && bar.io;
}

unsigned long pci_bar_start(struct pci_device *pci, unsigned int reg)
{
    struct ipxe_host_bar bar;

    if (!bar_for_register(pci, reg, &bar) || !bar.implemented) {
        return 0UL;
    }
    return bar.base;
}

unsigned long pci_bar_size(struct pci_device *pci, unsigned int reg)
{
    struct ipxe_host_bar bar;

    if (!bar_for_register(pci, reg, &bar) || !bar.implemented) {
        return 0UL;
    }
    return bar.size;
}

void pci_bar_set(struct pci_device *pci, unsigned int reg,
    unsigned long start)
{
    /* BAR assignment belongs to the claim; a driver may not move one. */
    (void)pci;
    (void)reg;
    (void)start;
}

static int config_read(struct pci_device *pci, unsigned int where,
    unsigned int width, uint32_t *value)
{
    if (pci == NULL || pci->openrfs == NULL ||
        !ipxe_host_config_read(pci->openrfs->handle, where, width, value)) {
        *value = UINT32_MAX;
        return -EIO;
    }
    return 0;
}

int pci_read_config_byte(struct pci_device *pci, unsigned int where,
    uint8_t *value)
{
    uint32_t dword;
    const int rc = config_read(pci, where, 1U, &dword);

    *value = (uint8_t)dword;
    return rc;
}

int pci_read_config_word(struct pci_device *pci, unsigned int where,
    uint16_t *value)
{
    uint32_t dword;
    const int rc = config_read(pci, where, 2U, &dword);

    *value = (uint16_t)dword;
    return rc;
}

int pci_read_config_dword(struct pci_device *pci, unsigned int where,
    uint32_t *value)
{
    return config_read(pci, where, 4U, value);
}

static int config_write(struct pci_device *pci, unsigned int where,
    unsigned int width, uint32_t value)
{
    if (pci == NULL || pci->openrfs == NULL ||
        !ipxe_host_config_write(pci->openrfs->handle, where, width, value)) {
        return -EACCES;
    }
    return 0;
}

int pci_write_config_byte(struct pci_device *pci, unsigned int where,
    uint8_t value)
{
    return config_write(pci, where, 1U, value);
}

int pci_write_config_word(struct pci_device *pci, unsigned int where,
    uint16_t value)
{
    return config_write(pci, where, 2U, value);
}

int pci_write_config_dword(struct pci_device *pci, unsigned int where,
    uint32_t value)
{
    return config_write(pci, where, 4U, value);
}

/*
 * iPXE's adjust_pci_device() turns on I/O and memory decode and bus
 * mastering, and raises a too-small latency timer. Here decode arrives with
 * the claim - I/O decode on request, memory decode when a BAR is mapped - and
 * bus mastering is granted against the layer's DMA arena.
 */
void adjust_pci_device(struct pci_device *pci)
{
    uint8_t latency = 0U;
    bool io_bar = false;

    if (pci == NULL || pci->openrfs == NULL) {
        return;
    }
    for (unsigned int reg = PCI_BASE_ADDRESS_0; reg <= PCI_BASE_ADDRESS_5;
         reg += 4U) {
        if (pci_bar_is_io(pci, reg)) {
            io_bar = true;
        }
    }
    if (io_bar) {
        (void)ipxe_host_enable_io(pci->openrfs->handle);
    }
    (void)ipxe_host_enable_bus_master(pci->openrfs->handle);
    if (pci_read_config_byte(pci, PCI_LATENCY_TIMER, &latency) == 0 &&
        latency < 32U) {
        (void)pci_write_config_byte(pci, PCI_LATENCY_TIMER, 32U);
    }
}

int pci_find_next_capability(struct pci_device *pci, int position,
    int capability)
{
    uint8_t next = 0U;

    if (pci_read_config_byte(pci, (unsigned int)position + PCI_CAP_NEXT,
            &next) != 0) {
        return 0;
    }
    for (unsigned int ttl = 0U; ttl < IPXE_GLUE_CAPABILITY_LIMIT && next != 0U;
         ++ttl) {
        uint8_t identifier = 0U;

        next &= (uint8_t)~3U;
        if (pci_read_config_byte(pci, next + PCI_CAP_ID, &identifier) != 0) {
            return 0;
        }
        if (identifier == 0xffU) {
            return 0;
        }
        if (identifier == (uint8_t)capability) {
            return next;
        }
        if (pci_read_config_byte(pci, next + PCI_CAP_NEXT, &next) != 0) {
            return 0;
        }
    }
    return 0;
}

int pci_find_capability(struct pci_device *pci, int capability)
{
    uint16_t status = 0U;
    uint8_t first = 0U;

    if (pci_read_config_word(pci, PCI_STATUS, &status) != 0 ||
        (status & PCI_STATUS_CAP_LIST) == 0U ||
        pci_read_config_byte(pci, PCI_CAPABILITY_LIST, &first) != 0 ||
        first == 0U) {
        return 0;
    }
    first &= (uint8_t)~3U;
    for (unsigned int ttl = 0U; ttl < IPXE_GLUE_CAPABILITY_LIMIT && first != 0U;
         ++ttl) {
        uint8_t identifier = 0U;
        uint8_t next = 0U;

        if (pci_read_config_byte(pci, first + PCI_CAP_ID, &identifier) != 0 ||
            identifier == 0xffU) {
            return 0;
        }
        if (identifier == (uint8_t)capability) {
            return first;
        }
        if (pci_read_config_byte(pci, first + PCI_CAP_NEXT, &next) != 0) {
            return 0;
        }
        first = (uint8_t)(next & ~3U);
    }
    return 0;
}

void pci_reset(struct pci_device *pci, unsigned int exp)
{
    uint16_t control = 0U;

    /* PCI Express function-level reset, as iPXE performs it. */
    if (pci_read_config_word(pci, exp + PCI_EXP_DEVCTL, &control) != 0) {
        return;
    }
    (void)pci_write_config_word(pci, exp + PCI_EXP_DEVCTL,
        (uint16_t)(control | PCI_EXP_DEVCTL_FLR));
    mdelay(PCI_EXP_FLR_DELAY_MS);
}

/* Ethernet. */

uint8_t eth_broadcast[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

void eth_init_addr(const void *hw_addr, void *ll_addr)
{
    memcpy(ll_addr, hw_addr, ETH_ALEN);
}

void eth_random_addr(void *hw_addr)
{
    uint8_t *address = hw_addr;

    for (size_t index = 0U; index < ETH_ALEN; ++index) {
        address[index] = (uint8_t)random();
    }
    address[0] &= (uint8_t)~0x01U;
    address[0] |= 0x02U;
}

const char *eth_ntoa(const void *ll_addr)
{
    static const char digits[] = "0123456789abcdef";
    static char text[ETH_ALEN * 3U];
    const uint8_t *address = ll_addr;

    for (size_t index = 0U; index < ETH_ALEN; ++index) {
        text[index * 3U] = digits[address[index] >> 4U];
        text[index * 3U + 1U] = digits[address[index] & 0x0fU];
        text[index * 3U + 2U] = index + 1U < ETH_ALEN ? ':' : '\0';
    }
    return text;
}

int eth_push(struct net_device *netdev, struct io_buffer *iobuf,
    const void *ll_dest, const void *ll_source, uint16_t net_proto)
{
    struct ethhdr *header = iob_push(iobuf, sizeof(*header));

    (void)netdev;
    memcpy(header->h_dest, ll_dest, ETH_ALEN);
    memcpy(header->h_source, ll_source, ETH_ALEN);
    header->h_protocol = net_proto;
    return 0;
}

int eth_pull(struct net_device *netdev, struct io_buffer *iobuf,
    const void **ll_dest, const void **ll_source, uint16_t *net_proto,
    unsigned int *flags)
{
    struct ethhdr *header = iobuf->data;

    (void)netdev;
    if (iob_len(iobuf) < sizeof(*header)) {
        return -EINVAL;
    }
    iob_pull(iobuf, sizeof(*header));
    *ll_dest = header->h_dest;
    *ll_source = header->h_source;
    *net_proto = header->h_protocol;
    *flags = 0U;
    return 0;
}

struct ll_protocol ethernet_protocol = {
    .name = "Ethernet",
    .push = eth_push,
    .pull = eth_pull,
    .init_addr = eth_init_addr,
    .ntoa = eth_ntoa,
    .ll_proto = 0x0100U,
    .hw_addr_len = ETH_ALEN,
    .ll_addr_len = ETH_ALEN,
    .ll_header_len = ETH_HLEN,
};

/* Network devices. */

static void free_netdev(struct refcnt *refcnt)
{
    struct net_device *netdev = container_of(refcnt, struct net_device,
        refcnt);

    free(netdev);
}

struct net_device *alloc_netdev(size_t priv_len)
{
    const size_t header = (sizeof(struct net_device) + 15U) & ~(size_t)15U;
    struct net_device *netdev = zalloc(header + priv_len);

    if (netdev == NULL) {
        return NULL;
    }
    ref_init(&netdev->refcnt, free_netdev);
    netdev->link_rc = GLUE_LINK_RC_UNKNOWN;
    INIT_LIST_HEAD(&netdev->list);
    INIT_LIST_HEAD(&netdev->open_list);
    INIT_LIST_HEAD(&netdev->tx_queue);
    INIT_LIST_HEAD(&netdev->tx_deferred);
    INIT_LIST_HEAD(&netdev->rx_queue);
    netdev->op = &null_netdev_operations;
    netdev->priv = (uint8_t *)netdev + header;
    return netdev;
}

struct net_device *alloc_etherdev(size_t priv_size)
{
    struct net_device *netdev = alloc_netdev(priv_size);

    if (netdev != NULL) {
        netdev->ll_protocol = &ethernet_protocol;
        netdev->ll_broadcast = eth_broadcast;
        netdev->max_pkt_len = ETH_FRAME_LEN;
        netdev->mtu = ETH_MAX_MTU;
    }
    return netdev;
}

const char *netdev_addr(struct net_device *netdev)
{
    return netdev->ll_protocol->ntoa(netdev->ll_addr);
}

void netdev_link_err(struct net_device *netdev, int rc)
{
    netdev->link_rc = rc;
}

void netdev_link_down(struct net_device *netdev)
{
    /* Keep a more detailed code if the driver already reported one. */
    if (netdev->link_rc == 0 || netdev->link_rc == GLUE_LINK_RC_UNKNOWN) {
        netdev_link_err(netdev, GLUE_LINK_RC_DOWN);
    }
}

static void record_stat(struct net_device_stats *stats, int rc)
{
    struct net_device_error *slot = NULL;

    if (rc == 0) {
        ++stats->good;
        return;
    }
    ++stats->bad;
    for (size_t index = 0U; index < NETDEV_MAX_UNIQUE_ERRORS; ++index) {
        if (stats->errors[index].rc == rc) {
            slot = &stats->errors[index];
            break;
        }
        if (slot == NULL && stats->errors[index].count == 0U) {
            slot = &stats->errors[index];
        }
    }
    if (slot != NULL) {
        slot->rc = rc;
        ++slot->count;
    }
}

int register_netdev(struct net_device *netdev)
{
    struct openrfs_ipxe_pci *owner = current_device;
    bool zero = true;

    if (owner == NULL || owner->netdev != NULL) {
        return -EBUSY;
    }
    for (size_t index = 0U; index < ETH_ALEN; ++index) {
        zero = zero && netdev->ll_addr[index] == 0U;
    }
    if (zero) {
        netdev->ll_protocol->init_addr(netdev->hw_addr, netdev->ll_addr);
    }
    netdev->openrfs_owner = owner;
    owner->netdev = netdev;
    return 0;
}

void unregister_netdev(struct net_device *netdev)
{
    struct openrfs_ipxe_pci *owner = netdev->openrfs_owner;

    netdev_close(netdev);
    if (owner != NULL) {
        owner->netdev = NULL;
    }
    netdev->openrfs_owner = NULL;
}

void netdev_irq(struct net_device *netdev, int enable)
{
    if (!netdev_irq_supported(netdev)) {
        return;
    }
    if ((enable != 0) == (netdev_irq_enabled(netdev) != 0)) {
        return;
    }
    netdev->op->irq(netdev, enable);
    if (enable) {
        netdev->state |= NETDEV_IRQ_ENABLED;
    } else {
        netdev->state &= ~NETDEV_IRQ_ENABLED;
    }
}

int netdev_open(struct net_device *netdev)
{
    int rc;

    if (netdev->state & NETDEV_OPEN) {
        return 0;
    }
    netdev->state |= NETDEV_OPEN;
    rc = netdev->op->open(netdev);
    if (rc != 0) {
        netdev->state &= ~NETDEV_OPEN;
    }
    return rc;
}

static void rx_flush(struct net_device *netdev);
static void tx_flush(struct net_device *netdev);

void netdev_close(struct net_device *netdev)
{
    if (!(netdev->state & NETDEV_OPEN)) {
        return;
    }
    netdev_irq(netdev, 0);
    netdev->op->close(netdev);
    tx_flush(netdev);
    rx_flush(netdev);
    netdev->state &= ~NETDEV_OPEN;
}

int netdev_tx(struct net_device *netdev, struct io_buffer *iobuf)
{
    int rc;

    list_add_tail(&iobuf->list, &netdev->tx_queue);
    if (netdev->state & NETDEV_TX_IN_PROGRESS) {
        rc = -EBUSY;
        goto err_busy;
    }
    netdev->state |= NETDEV_TX_IN_PROGRESS;
    if (!netdev_is_open(netdev)) {
        rc = -ENETUNREACH;
        goto err_closed;
    }
    if (netdev->dma != NULL && !dma_mapped(&iobuf->map)) {
        rc = iob_map_tx(iobuf, netdev->dma);
        if (rc != 0) {
            goto err_map;
        }
    }
    rc = netdev->op->transmit(netdev, iobuf);
    if (rc != 0) {
        goto err_transmit;
    }
    netdev->state &= ~NETDEV_TX_IN_PROGRESS;
    return 0;

err_transmit:
err_map:
err_closed:
    netdev->state &= ~NETDEV_TX_IN_PROGRESS;
err_busy:
    netdev_tx_complete_err(netdev, iobuf, rc);
    return rc;
}

void netdev_tx_err(struct net_device *netdev, struct io_buffer *iobuf, int rc)
{
    record_stat(&netdev->tx_stats, rc);
    if (iobuf != NULL && dma_mapped(&iobuf->map)) {
        iob_unmap(iobuf);
    }
    free_iob(iobuf);
}

void netdev_tx_defer(struct net_device *netdev, struct io_buffer *iobuf)
{
    list_del(&iobuf->list);
    list_add_tail(&iobuf->list, &netdev->tx_deferred);
    netdev_tx_err(netdev, NULL, -ENOBUFS);
}

void netdev_tx_complete_err(struct net_device *netdev,
    struct io_buffer *iobuf, int rc)
{
    list_del(&iobuf->list);
    netdev_tx_err(netdev, iobuf, rc);
    while ((iobuf = list_first_entry(&netdev->tx_deferred, struct io_buffer,
                list)) != NULL) {
        list_del(&iobuf->list);
        if (rc != 0) {
            netdev_tx_err(netdev, iobuf, -ECANCELED);
            continue;
        }
        (void)netdev_tx(netdev, iobuf);
        break;
    }
}

void netdev_tx_complete_next_err(struct net_device *netdev, int rc)
{
    struct io_buffer *iobuf = list_first_entry(&netdev->tx_queue,
        struct io_buffer, list);

    if (iobuf != NULL) {
        netdev_tx_complete_err(netdev, iobuf, rc);
    }
}

static void tx_flush(struct net_device *netdev)
{
    for (unsigned int guard = 0U; guard < 4096U &&
         !list_empty(&netdev->tx_queue); ++guard) {
        netdev_tx_complete_next_err(netdev, -ECANCELED);
    }
}

void netdev_rx(struct net_device *netdev, struct io_buffer *iobuf)
{
    if (dma_mapped(&iobuf->map)) {
        iob_unmap(iobuf);
    }
    if (netdev->openrfs_rx_queued >= IPXE_GLUE_RX_QUEUE_LIMIT) {
        /* The stack is not draining; drop rather than exhaust the arena. */
        netdev_rx_err(netdev, iobuf, -ENOBUFS);
        return;
    }
    list_add_tail(&iobuf->list, &netdev->rx_queue);
    ++netdev->openrfs_rx_queued;
    record_stat(&netdev->rx_stats, 0);
}

void netdev_rx_err(struct net_device *netdev, struct io_buffer *iobuf, int rc)
{
    if (iobuf != NULL && dma_mapped(&iobuf->map)) {
        iob_unmap(iobuf);
    }
    free_iob(iobuf);
    record_stat(&netdev->rx_stats, rc);
}

void netdev_poll(struct net_device *netdev)
{
    if (!(netdev->state & (NETDEV_OPEN | NETDEV_INSOMNIAC))) {
        return;
    }
    if (netdev->state & NETDEV_POLL_IN_PROGRESS) {
        return;
    }
    netdev->state |= NETDEV_POLL_IN_PROGRESS;
    netdev->op->poll(netdev);
    netdev->state &= ~NETDEV_POLL_IN_PROGRESS;
}

struct io_buffer *netdev_rx_dequeue(struct net_device *netdev)
{
    struct io_buffer *iobuf = list_first_entry(&netdev->rx_queue,
        struct io_buffer, list);

    if (iobuf == NULL) {
        return NULL;
    }
    list_del(&iobuf->list);
    if (netdev->openrfs_rx_queued > 0U) {
        --netdev->openrfs_rx_queued;
    }
    return iobuf;
}

static void rx_flush(struct net_device *netdev)
{
    struct io_buffer *iobuf;

    while ((iobuf = netdev_rx_dequeue(netdev)) != NULL) {
        netdev_rx_err(netdev, iobuf, -ECANCELED);
    }
}

/* Binding. */

size_t ipxe_glue_driver_count(void)
{
    return GLUE_DRIVER_COUNT;
}

const char *ipxe_glue_driver_name(size_t index)
{
    return index < GLUE_DRIVER_COUNT ? glue_drivers[index].name : NULL;
}

const char *ipxe_glue_driver_path(size_t index)
{
    return index < GLUE_DRIVER_COUNT ? glue_drivers[index].path : NULL;
}

static struct pci_device_id *match_driver(struct pci_driver *driver,
    const struct ipxe_host_pci_info *info)
{
    const uint32_t class = PCI_CLASS(info->class_code, info->subclass,
        info->prog_if);

    if (((driver->class.class ^ class) & driver->class.mask) != 0U) {
        return NULL;
    }
    for (unsigned int index = 0U; index < driver->id_count; ++index) {
        struct pci_device_id *id = &driver->ids[index];

        if ((id->vendor == PCI_ANY_ID || id->vendor == info->vendor_id) &&
            (id->device == PCI_ANY_ID || id->device == info->device_id)) {
            return id;
        }
    }
    return NULL;
}

static void describe_pci(struct openrfs_ipxe_pci *device,
    const struct ipxe_host_pci_info *info)
{
    struct pci_device *pci = &device->pci;

    memset(pci, 0, sizeof(*pci));
    pci->openrfs = device;
    pci->vendor = info->vendor_id;
    pci->device = info->device_id;
    pci->class = PCI_CLASS(info->class_code, info->subclass, info->prog_if);
    pci->hdrtype = info->header_type;
    pci->irq = info->interrupt_line;
    pci->busdevfn = PCI_BUSDEVFN(info->segment, info->bus, info->device,
        info->function);
    pci->dma.openrfs_arena = NULL;
    pci->dev.desc.bus_type = BUS_TYPE_PCI;
    pci->dev.desc.location = pci->busdevfn;
    pci->dev.desc.vendor = info->vendor_id;
    pci->dev.desc.device = info->device_id;
    pci->dev.desc.class = pci->class;
    pci->dev.desc.irq = info->interrupt_line;
    INIT_LIST_HEAD(&pci->dev.siblings);
    INIT_LIST_HEAD(&pci->dev.children);
    for (unsigned int reg = PCI_BASE_ADDRESS_0; reg <= PCI_BASE_ADDRESS_5;
         reg += 4U) {
        struct ipxe_host_bar bar;

        if (!bar_for_register(pci, reg, &bar) || !bar.implemented) {
            continue;
        }
        if (bar.io) {
            if (pci->ioaddr == 0UL) {
                pci->ioaddr = bar.base;
            }
        } else if (pci->membase == 0UL) {
            pci->membase = bar.base;
        }
        if (bar.is_64_bit) {
            reg += 4U;
        }
    }
    pci->dev.desc.ioaddr = pci->ioaddr;
}

bool ipxe_glue_try_bind(size_t index, const struct ipxe_host_pci_info *info)
{
    struct openrfs_ipxe_pci *device = NULL;

    for (size_t slot = 0U; slot < IPXE_GLUE_MAX_DEVICES; ++slot) {
        if (!devices[slot].bound && devices[slot].handle == NULL) {
            device = &devices[slot];
            break;
        }
    }
    if (device == NULL || info == NULL) {
        return false;
    }
    for (size_t driver_index = 0U; driver_index < GLUE_DRIVER_COUNT;
         ++driver_index) {
        const struct glue_driver *entry = &glue_drivers[driver_index];
        struct pci_device_id *id;
        char description[64];
        int rc;

        if (!ipxe_host_driver_enabled(entry->name)) {
            continue;
        }
        id = match_driver(entry->driver, info);
        if (id == NULL) {
            continue;
        }
        memset(device, 0, sizeof(*device));
        device->handle = ipxe_host_claim(index);
        if (device->handle == NULL) {
            return false;
        }
        device->entry = entry;
        device->function_index = index;
        describe_pci(device, info);
        device->pci.driver = entry->driver;
        device->pci.id = id;
        current_device = device;
        rc = entry->driver->probe(&device->pci);
        if (rc == 0 && device->netdev == NULL) {
            /* A driver that probed without registering has nothing to use. */
            entry->driver->remove(&device->pci);
            rc = -ENODEV;
        }
        if (rc == 0) {
            const int open_rc = netdev_open(device->netdev);

            if (open_rc != 0) {
                /* Stay bound; reset() retries the open when selected. */
                printf("OpenRFS: iPXE %s open failed: %s\n", entry->name,
                    strerror(open_rc));
            }
            snprintf(description, sizeof(description), "%s %s",
                entry->label, id->name);
            if (!ipxe_host_publish(device, device->handle, entry->name,
                    description, entry->path, device->instance,
                    sizeof(device->instance))) {
                netdev_close(device->netdev);
                entry->driver->remove(&device->pci);
                rc = -ENOBUFS;
            }
        }
        current_device = NULL;
        if (rc != 0) {
            ipxe_host_release(device->handle);
            memset(device, 0, sizeof(*device));
            return false;
        }
        memcpy(device->netdev->name, device->instance,
            sizeof(device->netdev->name) < sizeof(device->instance) ?
            sizeof(device->netdev->name) : sizeof(device->instance));
        device->netdev->name[sizeof(device->netdev->name) - 1U] = '\0';
        device->bound = true;
        return true;
    }
    return false;
}

/* The kernel's view of a bound device. */

enum ipxe_glue_result ipxe_glue_service(void *glue_device)
{
    struct openrfs_ipxe_pci *device = glue_device;
    struct net_device *netdev;

    if (device == NULL || !device->bound || device->netdev == NULL) {
        return IPXE_GLUE_FAILED;
    }
    netdev = device->netdev;
    current_device = device;
    ++device->polls;
    netdev_poll(netdev);
    current_device = NULL;
    return netdev_link_ok(netdev) ? IPXE_GLUE_OK : IPXE_GLUE_LINK_DOWN;
}

enum ipxe_glue_result ipxe_glue_transmit(void *glue_device,
    const uint8_t *frame, size_t length)
{
    struct openrfs_ipxe_pci *device = glue_device;
    struct io_buffer *iobuf;
    enum ipxe_glue_result serviced;
    int rc;

    if (length > ETH_FRAME_LEN) {
        return IPXE_GLUE_TOO_LARGE;
    }
    serviced = ipxe_glue_service(glue_device);
    if (serviced != IPXE_GLUE_OK) {
        return serviced;
    }
    iobuf = alloc_iob(length < ETH_ZLEN ? ETH_ZLEN : length);
    if (iobuf == NULL) {
        return IPXE_GLUE_TX_EXHAUSTED;
    }
    memcpy(iob_put(iobuf, length), frame, length);
    current_device = device;
    rc = netdev_tx(device->netdev, iobuf);
    current_device = NULL;
    if (rc == -ENOBUFS || rc == -EBUSY) {
        return IPXE_GLUE_TX_EXHAUSTED;
    }
    return rc == 0 ? IPXE_GLUE_OK : IPXE_GLUE_FAILED;
}

enum ipxe_glue_result ipxe_glue_receive(void *glue_device, uint8_t *frame,
    size_t capacity, size_t *length)
{
    struct openrfs_ipxe_pci *device = glue_device;
    struct io_buffer *iobuf;
    enum ipxe_glue_result serviced;
    size_t received;

    *length = 0U;
    serviced = ipxe_glue_service(glue_device);
    if (serviced != IPXE_GLUE_OK) {
        return serviced;
    }
    iobuf = netdev_rx_dequeue(device->netdev);
    if (iobuf == NULL) {
        return IPXE_GLUE_RX_EMPTY;
    }

    received = iob_len(iobuf);
    if (received > capacity) {
        free_iob(iobuf);
        return IPXE_GLUE_TOO_LARGE;
    }
    memcpy(frame, iobuf->data, received);
    *length = received;
    free_iob(iobuf);
    return IPXE_GLUE_OK;
}

enum ipxe_glue_result ipxe_glue_reset(void *glue_device)
{
    struct openrfs_ipxe_pci *device = glue_device;
    int rc;

    if (device == NULL || !device->bound || device->netdev == NULL) {
        return IPXE_GLUE_FAILED;
    }
    current_device = device;
    rc = netdev_open(device->netdev);
    if (rc == 0) {
        netdev_poll(device->netdev);
    }
    current_device = NULL;
    if (rc != 0) {
        return IPXE_GLUE_FAILED;
    }
    return netdev_link_ok(device->netdev) ? IPXE_GLUE_OK : IPXE_GLUE_LINK_DOWN;
}

enum ipxe_glue_result ipxe_glue_quiesce(void *glue_device)
{
    struct openrfs_ipxe_pci *device = glue_device;

    if (device == NULL || !device->bound || device->netdev == NULL) {
        return IPXE_GLUE_FAILED;
    }
    /* Stay bound and open; drop what the stopped stack will never read. */
    rx_flush(device->netdev);
    return IPXE_GLUE_OK;
}

void ipxe_glue_link(void *glue_device, struct ipxe_host_link *link)
{
    struct openrfs_ipxe_pci *device = glue_device;
    struct net_device *netdev;

    memset(link, 0, sizeof(*link));
    if (device == NULL || device->netdev == NULL) {
        return;
    }
    netdev = device->netdev;
    memcpy(link->mac, netdev->ll_addr, sizeof(link->mac));
    link->open = netdev_is_open(netdev) != 0;
    link->link_up = netdev_link_ok(netdev) != 0;
    link->rx_good = netdev->rx_stats.good;
    link->rx_bad = netdev->rx_stats.bad;
    link->tx_good = netdev->tx_stats.good;
    link->tx_bad = netdev->tx_stats.bad;
    link->polls = device->polls;
}
