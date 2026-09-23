/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_IPXE_HOST_H
#define OPENRFS_IPXE_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The boundary between the iPXE compatibility layer and the kernel.
 *
 * The vendored iPXE drivers are compiled in their own environment
 * (ports/ipxe/include), whose PCI and DMA vocabulary collides with OpenRFS's.
 * The two sides therefore never share a header except this one, which speaks
 * only plain C types. ports/ipxe/ipxe_glue.c implements the iPXE-facing half;
 * src/kernel/ipxe_host.c implements the kernel-facing half on top of the
 * driver framework, the DMA arena and the netdev layer.
 */

#define IPXE_HOST_INSTANCE_CAPACITY 16U

/* Plain status for glue calls. */
#define IPXE_HOST_OK 0
#define IPXE_HOST_FAILED (-1)

struct ipxe_host_pci_info {
    uint16_t segment;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint8_t interrupt_line;
    uint16_t vendor_id;
    uint16_t device_id;
};

struct ipxe_host_bar {
    unsigned long base;
    unsigned long size;
    bool implemented;
    bool io;
    bool is_64_bit;
};

/* What the kernel needs to know about one iPXE net_device. */
struct ipxe_host_link {
    uint8_t mac[6];
    bool open;
    bool link_up;
    uint64_t rx_good;
    uint64_t rx_bad;
    uint64_t tx_good;
    uint64_t tx_bad;
    uint64_t polls;
};

/* Glue result codes the kernel maps onto the netdev contract. */
enum ipxe_glue_result {
    IPXE_GLUE_OK = 0,
    IPXE_GLUE_LINK_DOWN,
    IPXE_GLUE_RX_EMPTY,
    IPXE_GLUE_TX_EXHAUSTED,
    IPXE_GLUE_TOO_LARGE,
    IPXE_GLUE_FAILED
};

/* Kernel-side services, implemented in src/kernel/ipxe_host.c. */
void *ipxe_host_alloc(size_t size, size_t alignment);
void ipxe_host_free(void *pointer);
size_t ipxe_host_allocation_size(const void *pointer);
bool ipxe_host_arena_contains(const void *pointer, size_t length);
void ipxe_host_delay_us(unsigned long microseconds);
unsigned long ipxe_host_ticks_ms(void);
void ipxe_glue_console_write(const char *text);

size_t ipxe_host_pci_count(void);
bool ipxe_host_pci_info(size_t index, struct ipxe_host_pci_info *info);
bool ipxe_host_pci_available(size_t index);
bool ipxe_host_driver_enabled(const char *name);
/* Claim function index; the handle names the claim in the calls below. */
void *ipxe_host_claim(size_t index);
void ipxe_host_release(void *handle);
bool ipxe_host_bar(void *handle, unsigned int bar_index,
    struct ipxe_host_bar *bar);
/* Map the BAR containing [bus_address, bus_address + length). */
void *ipxe_host_map(void *handle, unsigned long bus_address,
    size_t length);
bool ipxe_host_enable_io(void *handle);
bool ipxe_host_enable_bus_master(void *handle);
bool ipxe_host_config_read(void *handle, unsigned int offset,
    unsigned int width, uint32_t *value);
bool ipxe_host_config_write(void *handle, unsigned int offset,
    unsigned int width, uint32_t value);
/*
 * Publish a probed net_device: register it with the netdev layer and record
 * the framework binding (handle is NULL for an ISA card, which has no
 * claim). The instance name ("net0") is written back.
 */
bool ipxe_host_publish(void *glue_device, void *handle,
    const char *driver_name, const char *description,
    const char *source_path, char *instance, size_t instance_capacity);

/* Glue-side services, implemented in ports/ipxe/ipxe_glue.c. */
/* Every compiled driver: the PCI ones, then the ISA ones. */
size_t ipxe_glue_driver_count(void);
const char *ipxe_glue_driver_name(size_t index);
const char *ipxe_glue_driver_path(size_t index);
/* Try every compiled driver against PCI function index; true if bound. */
bool ipxe_glue_try_bind(size_t index,
    const struct ipxe_host_pci_info *info);
/* ISA drivers, probed at the addresses each lists (only when named). */
size_t ipxe_glue_isa_driver_count(void);
const char *ipxe_glue_isa_driver_name(size_t isa_index);
bool ipxe_glue_try_bind_isa(size_t isa_index);
enum ipxe_glue_result ipxe_glue_service(void *glue_device);
enum ipxe_glue_result ipxe_glue_transmit(void *glue_device,
    const uint8_t *frame, size_t length);
enum ipxe_glue_result ipxe_glue_receive(void *glue_device, uint8_t *frame,
    size_t capacity, size_t *length);
enum ipxe_glue_result ipxe_glue_reset(void *glue_device);
enum ipxe_glue_result ipxe_glue_quiesce(void *glue_device);
void ipxe_glue_link(void *glue_device, struct ipxe_host_link *link);

#endif
