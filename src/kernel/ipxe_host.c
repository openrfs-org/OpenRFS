/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The kernel-facing half of the iPXE compatibility layer: one DMA arena for
 * every iPXE device, PCI claims through the driver framework, and the netdev
 * operations that let the IPv4 stack use an iPXE driver's interface. See
 * include/openrfs/ipxe_host.h for the boundary.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/console.h>
#include <openrfs/dma_arena.h>
#include <openrfs/hwdrv.h>
#include <openrfs/hwdrv_layers.h>
#include <openrfs/ipxe_host.h>
#include <openrfs/netdev.h>
#include <openrfs/pci.h>
#include <openrfs/pci_resource.h>

/*
 * 4 MiB below 4 GiB: descriptor rings and packet buffers for every iPXE
 * interface bound at once, with each device's receive ring fully populated.
 */
#define IPXE_ARENA_PAGES 1024U
#define IPXE_ARENA_MAXIMUM_ADDRESS UINT64_C(0xFFFFFFFF)
#define IPXE_HOST_MAX_CLAIMS 8U

struct ipxe_host_claim {
    struct hwdrv_pci_device device;
    size_t function_index;
    bool active;
};

static const struct hwdrv_origin ipxe_origins[] = {
    { "iPXE", "744cdb451ef28bc894df72b6b40fdf1fda04acfc",
        "src/drivers/net/intel.c", "GPL-2.0-or-later OR UBDL-1.0" },
    { "iPXE", "744cdb451ef28bc894df72b6b40fdf1fda04acfc",
        "src/drivers/net/eepro100.c", "GPL-2.0-or-later" },
    { "iPXE", "744cdb451ef28bc894df72b6b40fdf1fda04acfc",
        "src/drivers/net/realtek.c", "GPL-2.0-or-later OR UBDL-1.0" },
    { "iPXE", "744cdb451ef28bc894df72b6b40fdf1fda04acfc",
        "src/drivers/net/pcnet32.c", "GPL-2.0-or-later" },
    { "iPXE", "744cdb451ef28bc894df72b6b40fdf1fda04acfc",
        "src/drivers/net/vmxnet3.c", "GPL-2.0-or-later OR UBDL-1.0" },
    { "iPXE", "744cdb451ef28bc894df72b6b40fdf1fda04acfc",
        "src/drivers/net/tulip.c", "GPL-1.0-or-later" },
    { "iPXE", "744cdb451ef28bc894df72b6b40fdf1fda04acfc",
        "src/drivers/net/ns8390.c", "BSD-2-Clause" },
    { "iPXE", "744cdb451ef28bc894df72b6b40fdf1fda04acfc",
        "src/drivers/net/ne2k_isa.c", "BSD-2-Clause" }
};

static struct dma_arena ipxe_arena;
static struct ipxe_host_claim claims[IPXE_HOST_MAX_CLAIMS];

static bool text_equal(const char *left, const char *right)
{
    size_t index = 0U;

    while (left[index] != '\0' && left[index] == right[index]) {
        ++index;
    }
    return left[index] == right[index];
}

static const struct hwdrv_origin *origin_for(const char *path)
{
    for (size_t index = 0U;
         index < sizeof(ipxe_origins) / sizeof(ipxe_origins[0]); ++index) {
        if (text_equal(ipxe_origins[index].path, path)) {
            return &ipxe_origins[index];
        }
    }
    return NULL;
}

static bool arena_ready(void)
{
    if (ipxe_arena.active) {
        return true;
    }
    return dma_arena_create(&ipxe_arena, IPXE_ARENA_PAGES,
        IPXE_ARENA_MAXIMUM_ADDRESS) == DMA_ARENA_STATUS_OK;
}

void *ipxe_host_alloc(size_t size, size_t alignment)
{
    if (!arena_ready()) {
        return NULL;
    }
    return dma_arena_allocate(&ipxe_arena, size, alignment);
}

void ipxe_host_free(void *pointer)
{
    (void)dma_arena_free(&ipxe_arena, pointer);
}

size_t ipxe_host_allocation_size(const void *pointer)
{
    return (size_t)dma_arena_allocation_size(&ipxe_arena, pointer);
}

bool ipxe_host_arena_contains(const void *pointer, size_t length)
{
    return dma_arena_contains(&ipxe_arena, pointer, length);
}

void ipxe_host_delay_us(unsigned long microseconds)
{
    hwdrv_delay_us(microseconds);
}

unsigned long ipxe_host_ticks_ms(void)
{
    return (unsigned long)(hwdrv_now_ns() / UINT64_C(1000000));
}

void ipxe_glue_console_write(const char *text)
{
    console_serial_write(text);
}

size_t ipxe_host_pci_count(void)
{
    return pci_function_count();
}

bool ipxe_host_pci_info(size_t index, struct ipxe_host_pci_info *info)
{
    const struct pci_function *function = pci_function_at(index);
    uint32_t line = 0U;

    if (function == NULL || info == NULL) {
        return false;
    }
    info->segment = function->address.segment;
    info->bus = function->address.bus;
    info->device = function->address.device;
    info->function = function->address.function;
    info->class_code = function->class_code;
    info->subclass = function->subclass;
    info->prog_if = function->prog_if;
    info->revision = function->revision;
    info->header_type = function->header_type;
    info->vendor_id = function->vendor_id;
    info->device_id = function->device_id;
    (void)hwdrv_pci_function_read(function, 0x3CU, 1U, &line);
    info->interrupt_line = (uint8_t)line;
    return true;
}

bool ipxe_host_pci_available(size_t index)
{
    const struct pci_function *function = pci_function_at(index);

    return function != NULL &&
        function->header_type == PCI_HEADER_TYPE_ENDPOINT &&
        !hwdrv_pci_function_claimed(function);
}

bool ipxe_host_driver_enabled(const char *name)
{
    return hwdrv_driver_enabled(name);
}

void *ipxe_host_claim(size_t index)
{
    const struct pci_function *function = pci_function_at(index);
    struct ipxe_host_claim *claim = NULL;

    if (function == NULL || !arena_ready()) {
        return NULL;
    }
    for (size_t slot = 0U; slot < IPXE_HOST_MAX_CLAIMS; ++slot) {
        if (!claims[slot].active) {
            claim = &claims[slot];
            break;
        }
    }
    if (claim == NULL ||
        hwdrv_pci_claim(function, &claim->device) != HWDRV_STATUS_OK) {
        return NULL;
    }
    claim->device.arena = &ipxe_arena;
    claim->function_index = index;
    claim->active = true;
    return claim;
}

void ipxe_host_release(void *handle)
{
    struct ipxe_host_claim *claim = handle;

    if (claim == NULL || !claim->active) {
        return;
    }
    (void)hwdrv_pci_release(&claim->device);
    claim->active = false;
}

bool ipxe_host_bar(void *handle, unsigned int bar_index,
    struct ipxe_host_bar *bar)
{
    struct ipxe_host_claim *claim = handle;
    const struct pci_bar_description *description;

    if (claim == NULL || !claim->active || bar == NULL ||
        bar_index >= PCI_BAR_COUNT) {
        return false;
    }
    description = pci_claim_bar(&claim->device.claim, (uint8_t)bar_index);
    if (description == NULL) {
        return false;
    }
    bar->base = (unsigned long)description->base;
    bar->size = (unsigned long)description->size;
    bar->implemented = description->implemented;
    bar->io = description->kind == PCI_BAR_IO;
    bar->is_64_bit = description->kind == PCI_BAR_MEMORY_64;
    return true;
}

void *ipxe_host_map(void *handle, unsigned long bus_address, size_t length)
{
    struct ipxe_host_claim *claim = handle;

    if (claim == NULL || !claim->active || length == 0U) {
        return NULL;
    }
    for (uint8_t index = 0U; index < PCI_BAR_COUNT; ++index) {
        const struct pci_bar_description *bar =
            pci_claim_bar(&claim->device.claim, index);
        volatile uint8_t *registers = NULL;
        uint64_t size = 0U;

        if (bar == NULL || !bar->implemented || bar->kind == PCI_BAR_IO ||
            bus_address < bar->base ||
            bus_address - bar->base >= bar->size ||
            length > bar->size - (bus_address - bar->base)) {
            continue;
        }
        if (hwdrv_pci_map_bar(&claim->device, index, &registers, &size) !=
                HWDRV_STATUS_OK || registers == NULL) {
            return NULL;
        }
        return (void *)(uintptr_t)(registers + (bus_address - bar->base));
    }
    return NULL;
}

bool ipxe_host_enable_io(void *handle)
{
    struct ipxe_host_claim *claim = handle;

    if (claim == NULL || !claim->active) {
        return false;
    }
    for (uint8_t index = 0U; index < PCI_BAR_COUNT; ++index) {
        const struct pci_bar_description *bar =
            pci_claim_bar(&claim->device.claim, index);
        uint16_t port = 0U;

        if (bar != NULL && bar->implemented && bar->kind == PCI_BAR_IO) {
            return hwdrv_pci_io_bar(&claim->device, index, &port, NULL) ==
                HWDRV_STATUS_OK;
        }
    }
    return false;
}

bool ipxe_host_enable_bus_master(void *handle)
{
    struct ipxe_host_claim *claim = handle;

    if (claim == NULL || !claim->active) {
        return false;
    }
    return hwdrv_pci_enable_bus_master(&claim->device, &ipxe_arena) ==
        HWDRV_STATUS_OK;
}

bool ipxe_host_config_read(void *handle, unsigned int offset,
    unsigned int width, uint32_t *value)
{
    struct ipxe_host_claim *claim = handle;

    if (claim == NULL || !claim->active || offset > UINT16_MAX) {
        return false;
    }
    return hwdrv_pci_config_read(&claim->device, (uint16_t)offset, width,
        value);
}

bool ipxe_host_config_write(void *handle, unsigned int offset,
    unsigned int width, uint32_t value)
{
    struct ipxe_host_claim *claim = handle;

    if (claim == NULL || !claim->active || offset > UINT16_MAX) {
        return false;
    }
    return hwdrv_pci_config_write(&claim->device, (uint16_t)offset, width,
        value);
}

static enum virtio_net_status map_result(enum ipxe_glue_result result)
{
    switch (result) {
    case IPXE_GLUE_OK:
        return VIRTIO_NET_STATUS_OK;
    case IPXE_GLUE_LINK_DOWN:
        return VIRTIO_NET_STATUS_LINK_DOWN;
    case IPXE_GLUE_RX_EMPTY:
        return VIRTIO_NET_STATUS_RX_EMPTY;
    case IPXE_GLUE_TX_EXHAUSTED:
        return VIRTIO_NET_STATUS_TX_EXHAUSTED;
    case IPXE_GLUE_TOO_LARGE:
        return VIRTIO_NET_STATUS_FRAME_TOO_LARGE;
    case IPXE_GLUE_FAILED:
    default:
        return VIRTIO_NET_STATUS_BAD_COMPLETION;
    }
}

static enum virtio_net_status operation_service(void *context)
{
    return map_result(ipxe_glue_service(context));
}

static enum virtio_net_status operation_reset(void *context)
{
    return map_result(ipxe_glue_reset(context));
}

static enum virtio_net_status operation_transmit(void *context,
    const uint8_t *frame, size_t length)
{
    return map_result(ipxe_glue_transmit(context, frame, length));
}

static enum virtio_net_status operation_receive(void *context,
    uint8_t *frame, size_t capacity, size_t *length)
{
    return map_result(ipxe_glue_receive(context, frame, capacity, length));
}

static struct virtio_net_state operation_state(void *context)
{
    struct virtio_net_state state;
    struct ipxe_host_link link;
    uint8_t *bytes = (uint8_t *)&state;

    for (size_t index = 0U; index < sizeof(state); ++index) {
        bytes[index] = 0U;
    }
    ipxe_glue_link(context, &link);
    for (size_t index = 0U; index < sizeof(state.mac); ++index) {
        state.mac[index] = link.mac[index];
    }
    state.present = true;
    state.active = link.open;
    state.link_up = link.link_up;
    state.polling_fallback = true;
    state.statistics.rx_frames = link.rx_good;
    state.statistics.tx_frames = link.tx_good;
    state.statistics.dropped_frames = link.rx_bad;
    state.statistics.malformed_frames = link.tx_bad;
    state.statistics.polling_passes = link.polls;
    return state;
}

static enum virtio_net_status operation_shutdown(void *context)
{
    return map_result(ipxe_glue_quiesce(context));
}

static const struct netdev_operations ipxe_netdev_operations = {
    .service = operation_service,
    .reset = operation_reset,
    .transmit = operation_transmit,
    .receive = operation_receive,
    .state = operation_state,
    .shutdown = operation_shutdown
};

bool ipxe_host_publish(void *glue_device, void *handle,
    const char *driver_name, const char *description,
    const char *source_path, char *instance, size_t instance_capacity)
{
    struct ipxe_host_claim *claim = handle;
    struct netdev_interface_info info;
    const struct hwdrv_origin *origin = origin_for(source_path);
    size_t index = 0U;

    if ((claim != NULL && !claim->active) || origin == NULL ||
        instance == NULL || instance_capacity < NETDEV_NAME_CAPACITY) {
        return false;
    }
    if (netdev_register(driver_name, &ipxe_netdev_operations, glue_device,
            &index) != VIRTIO_NET_STATUS_OK ||
        !netdev_interface_info(index, &info)) {
        return false;
    }
    for (size_t byte = 0U; byte < NETDEV_NAME_CAPACITY; ++byte) {
        instance[byte] = info.name[byte];
        if (info.name[byte] == '\0') {
            break;
        }
    }
    instance[instance_capacity - 1U] = '\0';
    if (hwdrv_record_binding(driver_name, info.name, description, origin,
            HWDRV_CLASS_NETWORK, claim != NULL ? claim->device.function :
                NULL) != HWDRV_STATUS_OK) {
        return false;
    }
    console_write("OpenRFS: ");
    console_write(info.name);
    console_write(" bound by iPXE ");
    console_write(driver_name);
    console_write(": ");
    console_write(description);
    console_putc('\n');
    return true;
}

size_t ipxe_layer_driver_count(void)
{
    return ipxe_glue_driver_count();
}

const char *ipxe_layer_driver_name(size_t index)
{
    return ipxe_glue_driver_name(index);
}

enum hwdrv_status ipxe_layer_bind_all(void)
{
    size_t bound = 0U;

    for (size_t index = 0U; index < pci_function_count(); ++index) {
        struct ipxe_host_pci_info info;

        if (!ipxe_host_pci_available(index) ||
            !ipxe_host_pci_info(index, &info)) {
            continue;
        }
        if (ipxe_glue_try_bind(index, &info)) {
            ++bound;
        }
    }
    for (size_t index = 0U; index < ipxe_glue_isa_driver_count(); ++index) {
        if (hwdrv_get_mode() == HWDRV_MODE_SELECTED &&
            hwdrv_driver_enabled(ipxe_glue_isa_driver_name(index)) &&
            ipxe_glue_try_bind_isa(index)) {
            ++bound;
        }
    }
    return bound != 0U ? HWDRV_STATUS_OK : HWDRV_STATUS_ABSENT;
}
