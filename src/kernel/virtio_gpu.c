/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/clock.h>
#include <openrfs/console.h>
#include <openrfs/cpu.h>
#include <openrfs/dma.h>
#include <openrfs/framebuffer.h>
#include <openrfs/pci.h>
#include <openrfs/pci_resource.h>
#include <openrfs/virtio_gpu.h>
#include <openrfs/virtio_gpu_protocol.h>

#define GPU_VENDOR UINT16_C(0x1AF4)
#define GPU_DEVICE UINT16_C(0x1050)
#define GPU_VERSION_1 (UINT64_C(1) << 32U)
#define GPU_MAX_DIMENSION 2048U
#define GPU_MAX_PAGES 4096U
#define GPU_QUEUE_SIZE 8U
#define GPU_TIMEOUT_NS UINT64_C(100000000)
#define GPU_RESOURCE_ID 1U
#define GPU_FENCE_FLAG 1U
#define GPU_RESP_NODATA UINT32_C(0x1100)
#define GPU_RESP_DISPLAY_INFO UINT32_C(0x1101)
#define GPU_CMD_DISPLAY_INFO UINT32_C(0x0100)
#define GPU_CMD_CREATE_2D UINT32_C(0x0101)
#define GPU_CMD_UNREF UINT32_C(0x0102)
#define GPU_CMD_SET_SCANOUT UINT32_C(0x0103)
#define GPU_CMD_FLUSH UINT32_C(0x0104)
#define GPU_CMD_TRANSFER UINT32_C(0x0105)
#define GPU_CMD_ATTACH UINT32_C(0x0106)
#define GPU_CMD_DETACH UINT32_C(0x0107)
#define GPU_FORMAT_B8G8R8X8 2U
#define GPU_RESPONSE_BYTES 408U

#define COMMON_FEATURE_SELECT 0U
#define COMMON_FEATURE 4U
#define COMMON_DRIVER_FEATURE_SELECT 8U
#define COMMON_DRIVER_FEATURE 12U
#define COMMON_NUM_QUEUES 18U
#define COMMON_STATUS 20U
#define COMMON_GENERATION 21U
#define COMMON_QUEUE_SELECT 22U
#define COMMON_QUEUE_SIZE 24U
#define COMMON_QUEUE_ENABLE 28U
#define COMMON_QUEUE_NOTIFY_OFF 30U
#define COMMON_QUEUE_DESC 32U
#define COMMON_QUEUE_DRIVER 40U
#define COMMON_QUEUE_DEVICE 48U
#define COMMON_BYTES 56U
#define DEVICE_SCANOUTS 8U
#define DEVICE_BYTES 16U

struct gpu_region {
    uint32_t offset;
    uint32_t length;
    uint32_t multiplier;
    uint8_t bar;
    volatile uint8_t *base;
    bool found;
};

struct gpu_runtime {
    struct virtio_gpu_output_state state;
    struct pci_device_claim claim;
    struct dma_allocation queue;
    struct dma_allocation command;
    struct dma_allocation backing;
    struct gpu_region common;
    struct gpu_region notify;
    struct gpu_region device;
    uint64_t notify_displacement;
    uint64_t next_fence;
    uint16_t available_index;
    uint16_t used_index;
    uint32_t pitch;
    bool attempted;
    bool bus_master;
    bool resource_created;
    bool backing_attached;
    bool scanout_set;
};

static struct gpu_runtime gpu;

static void clear(void *pointer, size_t length)
{
    uint8_t *bytes = pointer;
    for (size_t index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

static void put16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *bytes, uint32_t value)
{
    put16(bytes, (uint16_t)value);
    put16(bytes + 2U, (uint16_t)(value >> 16U));
}

static void put64(uint8_t *bytes, uint64_t value)
{
    put32(bytes, (uint32_t)value);
    put32(bytes + 4U, (uint32_t)(value >> 32U));
}

static uint16_t get16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8U;
}

static uint32_t get32(const uint8_t *bytes)
{
    return (uint32_t)get16(bytes) | (uint32_t)get16(bytes + 2U) << 16U;
}

static uint8_t mmio8(volatile uint8_t *base, size_t offset)
{
    return base[offset];
}

static uint16_t mmio16(volatile uint8_t *base, size_t offset)
{
    return *(volatile uint16_t *)(void *)(base + offset);
}

static uint32_t mmio32(volatile uint8_t *base, size_t offset)
{
    return *(volatile uint32_t *)(void *)(base + offset);
}

static void write8(volatile uint8_t *base, size_t offset, uint8_t value)
{
    base[offset] = value;
}

static void write16(volatile uint8_t *base, size_t offset, uint16_t value)
{
    *(volatile uint16_t *)(void *)(base + offset) = value;
}

static void write32(volatile uint8_t *base, size_t offset, uint32_t value)
{
    *(volatile uint32_t *)(void *)(base + offset) = value;
}

static void write64(volatile uint8_t *base, size_t offset, uint64_t value)
{
    *(volatile uint64_t *)(void *)(base + offset) = value;
}

static bool config32(const struct pci_function *function, uint16_t offset,
    uint32_t *value)
{
    return pci_config_read_port(function->address, offset, value) ==
        PCI_STATUS_OK;
}

static bool config8(const struct pci_function *function, uint16_t offset,
    uint8_t *value)
{
    uint32_t word;
    if (!config32(function, (uint16_t)(offset & ~UINT16_C(3)), &word)) {
        return false;
    }
    *value = (uint8_t)(word >> ((offset & 3U) * 8U));
    return true;
}

static bool message_interrupts_disabled(const struct pci_function *function)
{
    for (size_t index = 0U; index < function->capability_count; ++index) {
        const struct pci_capability *cap = &function->capabilities[index];
        uint8_t control;
        if (cap->identifier != PCI_CAPABILITY_MSI &&
            cap->identifier != PCI_CAPABILITY_MSI_X) {
            continue;
        }
        if (!config8(function, (uint16_t)(cap->offset +
                (cap->identifier == PCI_CAPABILITY_MSI ? 2U : 3U)),
                &control) ||
            (control & (cap->identifier == PCI_CAPABILITY_MSI ?
                UINT8_C(0x01) : UINT8_C(0x80))) != 0U) {
            return false;
        }
    }
    return true;
}

static bool collect_regions(const struct pci_function *function)
{
    for (size_t index = 0U; index < function->capability_count; ++index) {
        const struct pci_capability *cap = &function->capabilities[index];
        struct gpu_region *region;
        uint8_t length;
        uint8_t type;
        uint32_t offset;
        uint32_t bytes;
        uint32_t multiplier = 0U;
        uint8_t bar;

        if (cap->identifier != PCI_CAPABILITY_VENDOR) {
            continue;
        }
        if (!config8(function, (uint16_t)(cap->offset + 2U), &length) ||
            !config8(function, (uint16_t)(cap->offset + 3U), &type) ||
            length < 16U || (uint16_t)cap->offset + length >
                PCI_CONFIG_SPACE_SIZE) {
            return false;
        }
        if (type == 1U) {
            region = &gpu.common;
        } else if (type == 2U) {
            region = &gpu.notify;
        } else if (type == 4U) {
            region = &gpu.device;
        } else {
            continue;
        }
        if (region->found ||
            !config8(function, (uint16_t)(cap->offset + 4U), &bar) ||
            !config32(function, (uint16_t)(cap->offset + 8U), &offset) ||
            !config32(function, (uint16_t)(cap->offset + 12U), &bytes) ||
            (type == 2U && (length < 20U ||
                !config32(function, (uint16_t)(cap->offset + 16U),
                    &multiplier))) ||
            bar >= PCI_BAR_COUNT || bytes == 0U) {
            return false;
        }
        region->found = true;
        region->bar = bar;
        region->offset = offset;
        region->length = bytes;
        region->multiplier = multiplier;
    }
    return gpu.common.found && gpu.notify.found && gpu.device.found &&
        gpu.common.length >= COMMON_BYTES &&
        gpu.notify.length >= sizeof(uint16_t) &&
        gpu.device.length >= DEVICE_BYTES;
}

static bool map_region(struct gpu_region *region)
{
    struct pci_mmio_region *mapping = pci_claim_mapped_bar(&gpu.claim,
        region->bar);
    volatile void *pointer = NULL;
    if (mapping == NULL && pci_claim_map_bar(&gpu.claim, region->bar,
            &mapping) != PCI_RESOURCE_STATUS_OK) {
        return false;
    }
    if (pci_mmio_subregion(mapping, region->offset, region->length,
            &pointer) != PCI_RESOURCE_STATUS_OK) {
        return false;
    }
    region->base = pointer;
    return true;
}

static bool reset_device(void)
{
    if (gpu.common.base == NULL) {
        return false;
    }
    write8(gpu.common.base, COMMON_STATUS, 0U);
    for (size_t spin = 0U; spin < 1000000U; ++spin) {
        if (mmio8(gpu.common.base, COMMON_STATUS) == 0U) {
            return true;
        }
        __asm__ volatile ("pause" : : : "memory");
    }
    return false;
}

static bool negotiate(void)
{
    uint64_t features;
    write8(gpu.common.base, COMMON_STATUS, 3U);
    write32(gpu.common.base, COMMON_FEATURE_SELECT, 0U);
    features = mmio32(gpu.common.base, COMMON_FEATURE);
    write32(gpu.common.base, COMMON_FEATURE_SELECT, 1U);
    features |= (uint64_t)mmio32(gpu.common.base, COMMON_FEATURE) << 32U;
    if ((features & GPU_VERSION_1) == 0U) {
        return false;
    }
    write32(gpu.common.base, COMMON_DRIVER_FEATURE_SELECT, 0U);
    write32(gpu.common.base, COMMON_DRIVER_FEATURE, 0U);
    write32(gpu.common.base, COMMON_DRIVER_FEATURE_SELECT, 1U);
    write32(gpu.common.base, COMMON_DRIVER_FEATURE, 1U);
    write8(gpu.common.base, COMMON_STATUS, 11U);
    if ((mmio8(gpu.common.base, COMMON_STATUS) & 8U) == 0U) {
        return false;
    }
    gpu.state.negotiated_features = GPU_VERSION_1;
    return true;
}

static bool allocate_dma(uint32_t width, uint32_t height)
{
    const uint64_t size = (uint64_t)width * height * 4U;
    const size_t pages = (size_t)((size + OPENRFS_PAGE_SIZE - 1U) /
        OPENRFS_PAGE_SIZE);
    const struct dma_request small = { 1U, OPENRFS_PAGE_SIZE, UINT32_MAX };
    const struct dma_request pixels = { pages, OPENRFS_PAGE_SIZE,
        UINT32_MAX };
    if (pages == 0U || pages > GPU_MAX_PAGES ||
        dma_allocate(&small, &gpu.queue) != DMA_STATUS_OK ||
        dma_allocate(&small, &gpu.command) != DMA_STATUS_OK ||
        dma_allocate(&pixels, &gpu.backing) != DMA_STATUS_OK) {
        return false;
    }
    clear(gpu.queue.cpu_address, (size_t)gpu.queue.byte_length);
    /* Split-ring NO_INTERRUPT: this driver polls and owns no IRQ vector. */
    put16((uint8_t *)gpu.queue.cpu_address + 128U, 1U);
    clear(gpu.command.cpu_address, (size_t)gpu.command.byte_length);
    clear(gpu.backing.cpu_address, (size_t)gpu.backing.byte_length);
    gpu.pitch = width * 4U;
    gpu.state.resource_pages = pages;
    return dma_mark_initialized(&gpu.queue) == DMA_STATUS_OK &&
        dma_mark_initialized(&gpu.command) == DMA_STATUS_OK &&
        dma_mark_initialized(&gpu.backing) == DMA_STATUS_OK &&
        dma_transfer_to_device(&gpu.queue) == DMA_STATUS_OK &&
        dma_transfer_to_device(&gpu.command) == DMA_STATUS_OK &&
        dma_transfer_to_device(&gpu.backing) == DMA_STATUS_OK;
}

static bool configure_queue(void)
{
    const uint64_t base = gpu.queue.frames.physical_base;
    const uint16_t available_offset = GPU_QUEUE_SIZE * 16U;
    const uint16_t used_offset = 256U;
    uint16_t notify_offset;
    write16(gpu.common.base, COMMON_QUEUE_SELECT, 0U);
    if (mmio16(gpu.common.base, COMMON_NUM_QUEUES) < 1U ||
        mmio16(gpu.common.base, COMMON_QUEUE_SIZE) < GPU_QUEUE_SIZE ||
        mmio16(gpu.common.base, COMMON_QUEUE_ENABLE) != 0U) {
        return false;
    }
    write16(gpu.common.base, COMMON_QUEUE_SIZE, GPU_QUEUE_SIZE);
    write64(gpu.common.base, COMMON_QUEUE_DESC, base);
    write64(gpu.common.base, COMMON_QUEUE_DRIVER, base + available_offset);
    write64(gpu.common.base, COMMON_QUEUE_DEVICE, base + used_offset);
    notify_offset = mmio16(gpu.common.base, COMMON_QUEUE_NOTIFY_OFF);
    gpu.notify_displacement = (uint64_t)notify_offset *
        gpu.notify.multiplier;
    if (gpu.notify_displacement > gpu.notify.length ||
        gpu.notify.length - gpu.notify_displacement < 2U) {
        return false;
    }
    write16(gpu.common.base, COMMON_QUEUE_ENABLE, 1U);
    return mmio16(gpu.common.base, COMMON_QUEUE_ENABLE) == 1U;
}

static bool command(uint32_t type, const uint8_t *payload, uint32_t length,
    uint32_t expected_type, uint32_t response_length)
{
    uint8_t *io = gpu.command.cpu_address;
    uint8_t *ring = gpu.queue.cpu_address;
    uint8_t *response = io + 512U;
    const uint64_t fence = ++gpu.next_fence;
    const uint64_t physical = gpu.command.frames.physical_base;
    uint64_t deadline;
    uint64_t submitted_at;
    uint16_t used_index;

    if (length > 512U - 24U || response_length < 24U ||
        response_length > GPU_RESPONSE_BYTES || payload == NULL ||
        fence == 0U || !gpu.bus_master ||
        (mmio8(gpu.common.base, COMMON_STATUS) & 64U) != 0U) {
        return false;
    }
    clear(io, 512U + GPU_RESPONSE_BYTES);
    put32(io, type);
    put32(io + 4U, GPU_FENCE_FLAG);
    put64(io + 8U, fence);
    for (uint32_t index = 0U; index < length; ++index) {
        io[24U + index] = payload[index];
    }
    put64(ring, physical);
    put32(ring + 8U, 24U + length);
    put16(ring + 12U, 1U);
    put16(ring + 14U, 1U);
    put64(ring + 16U, physical + 512U);
    put32(ring + 24U, response_length);
    put16(ring + 28U, 2U);
    put16(ring + 30U, 0U);
    put16(ring + 128U + 4U +
        (gpu.available_index & (GPU_QUEUE_SIZE - 1U)) * 2U, 0U);
    cpu_store_fence();
    ++gpu.available_index;
    put16(ring + 128U + 2U, gpu.available_index);
    cpu_store_fence();
    write16(gpu.notify.base, (size_t)gpu.notify_displacement, 0U);
    ++gpu.state.queued_commands;
    gpu.state.peak_queue_occupancy = 1U;
    submitted_at = clock_monotonic_ns();
    deadline = submitted_at > UINT64_MAX - GPU_TIMEOUT_NS ?
        UINT64_MAX : submitted_at + GPU_TIMEOUT_NS;
    do {
        used_index = *(volatile uint16_t *)(void *)(ring + 258U);
        if (!virtio_gpu_ring_progress_valid(used_index, gpu.used_index,
                1U)) {
            break;
        }
        if (used_index != gpu.used_index) {
            const uint8_t *element = ring + 260U +
                (gpu.used_index & (GPU_QUEUE_SIZE - 1U)) * 8U;
            __asm__ volatile ("lfence" : : : "memory");
            ++gpu.used_index;
            if (!virtio_gpu_response_valid(response, GPU_RESPONSE_BYTES,
                    get32(element), get32(element + 4U), expected_type,
                    response_length, fence)) {
                break;
            }
            ++gpu.state.completed_commands;
            gpu.state.last_command_ns = clock_monotonic_ns() - submitted_at;
            if (gpu.state.last_command_ns > gpu.state.max_command_ns) {
                gpu.state.max_command_ns = gpu.state.last_command_ns;
            }
            return true;
        }
        __asm__ volatile ("pause" : : : "memory");
    } while (clock_monotonic_ns() < deadline);
    ++gpu.state.failed_commands;
    gpu.state.last_command_ns = clock_monotonic_ns() - submitted_at;
    gpu.state.reset_reason = type;
    return false;
}

static bool simple_command(uint32_t type, const uint8_t *payload,
    uint32_t length)
{
    return command(type, payload, length, GPU_RESP_NODATA, 24U);
}

static void rectangle(uint8_t *bytes, uint32_t x, uint32_t y,
    uint32_t width, uint32_t height)
{
    put32(bytes, x);
    put32(bytes + 4U, y);
    put32(bytes + 8U, width);
    put32(bytes + 12U, height);
}

static bool setup_resource(uint32_t width, uint32_t height)
{
    uint8_t payload[24] = { 0 };
    put32(payload, GPU_RESOURCE_ID);
    put32(payload + 4U, GPU_FORMAT_B8G8R8X8);
    put32(payload + 8U, width);
    put32(payload + 12U, height);
    if (!simple_command(GPU_CMD_CREATE_2D, payload, 16U)) {
        return false;
    }
    gpu.resource_created = true;
    clear(payload, sizeof(payload));
    put32(payload, GPU_RESOURCE_ID);
    put32(payload + 4U, 1U);
    put64(payload + 8U, gpu.backing.frames.physical_base);
    put32(payload + 16U, (uint32_t)gpu.backing.byte_length);
    if (!simple_command(GPU_CMD_ATTACH, payload, sizeof(payload))) {
        return false;
    }
    gpu.backing_attached = true;
    clear(payload, sizeof(payload));
    rectangle(payload, 0U, 0U, width, height);
    put32(payload + 16U, 0U);
    put32(payload + 20U, GPU_RESOURCE_ID);
    if (!simple_command(GPU_CMD_SET_SCANOUT, payload, sizeof(payload))) {
        return false;
    }
    gpu.scanout_set = true;
    return true;
}

static bool scanout_accepts(uint32_t width, uint32_t height)
{
    const uint8_t empty = 0U;
    uint8_t *response;
    if (!command(GPU_CMD_DISPLAY_INFO, &empty, 0U,
            GPU_RESP_DISPLAY_INFO, GPU_RESPONSE_BYTES)) {
        return false;
    }
    response = (uint8_t *)gpu.command.cpu_address + 512U;
    return get32(response + 40U) == 1U &&
        get32(response + 24U) == 0U &&
        get32(response + 28U) == 0U &&
        get32(response + 32U) >= width &&
        get32(response + 36U) >= height;
}

static bool stop_dma(void)
{
    const bool interrupts = cpu_interrupts_enabled();
    bool stopped = !gpu.claim.bus_master_enabled;
    if (interrupts) {
        cpu_interrupt_disable();
    }
    if (gpu.claim.bus_master_enabled &&
        pci_claim_disable_bus_master(&gpu.claim) == PCI_RESOURCE_STATUS_OK) {
        stopped = true;
    }
    if (interrupts) {
        cpu_interrupt_enable();
    }
    gpu.bus_master = !stopped;
    return stopped;
}

static void release_dma(struct dma_allocation *allocation)
{
    if (!allocation->active) {
        return;
    }
    if (allocation->owner == DMA_OWNER_DEVICE) {
        (void)dma_transfer_to_cpu(allocation);
    }
    (void)dma_release(allocation);
}

static void fail_output(uint32_t reason)
{
    uint8_t payload[24] = { 0 };
    bool reset = false;
    gpu.state.status = VIRTIO_GPU_OUTPUT_FAILED;
    gpu.state.reset_reason = reason;
    gpu.state.output_refused = gpu.scanout_set ||
        gpu.state.frames_presented != 0U;
    gpu.state.loader_framebuffer_selected = !gpu.state.output_refused;
    if (reason != 14U) {
        console_serial_write("OpenRFS: VirtIO GPU output failed reason ");
        console_serial_write_u64(reason);
        console_serial_write("\n");
    }
    if (gpu.bus_master && gpu.state.failed_commands == 0U) {
        if (gpu.scanout_set) {
            if (!simple_command(GPU_CMD_SET_SCANOUT, payload, 24U)) {
                goto reset;
            }
        }
        put32(payload, GPU_RESOURCE_ID);
        if (gpu.backing_attached) {
            if (!simple_command(GPU_CMD_DETACH, payload, 8U)) {
                goto reset;
            }
        }
        if (gpu.resource_created) {
            (void)simple_command(GPU_CMD_UNREF, payload, 8U);
        }
    }
reset:
    if (gpu.common.base != NULL) {
        reset = reset_device();
    }
    if (!stop_dma() || (gpu.common.base != NULL && !reset)) {
        return;
    }
    release_dma(&gpu.backing);
    release_dma(&gpu.command);
    release_dma(&gpu.queue);
    gpu.state.resource_pages = 0U;
    if (gpu.claim.active) {
        const bool interrupts = cpu_interrupts_enabled();
        if (interrupts) {
            cpu_interrupt_disable();
        }
        (void)pci_release_device(&gpu.claim);
        if (interrupts) {
            cpu_interrupt_enable();
        }
    }
}

static bool device_still_present(void)
{
    bool changed = false;
    if (pci_claim_device_changed(&gpu.claim, &changed) ==
            PCI_RESOURCE_STATUS_OK && !changed) {
        return true;
    }
    gpu.state.status = VIRTIO_GPU_OUTPUT_FAILED;
    gpu.state.output_refused = true;
    gpu.state.loader_framebuffer_selected = false;
    gpu.state.reset_reason = 13U;
    if (changed) {
        const bool interrupts = cpu_interrupts_enabled();
        if (interrupts) {
            cpu_interrupt_disable();
        }
        const bool released = pci_release_changed_device(&gpu.claim) ==
            PCI_RESOURCE_STATUS_OK;
        if (interrupts) {
            cpu_interrupt_enable();
        }
        if (released) {
            gpu.bus_master = false;
            release_dma(&gpu.backing);
            release_dma(&gpu.command);
            release_dma(&gpu.queue);
            gpu.state.resource_pages = 0U;
        }
    }
    return false;
}

void virtio_gpu_output_start(uint32_t width, uint32_t height)
{
    const struct pci_function *function;
    struct pci_bus_master_request master = { 0 };
    struct framebuffer_state framebuffer;
    uint32_t identity;
    uint32_t command_register;
    uint32_t scanouts;
    uint8_t generation;
    bool interrupts;

    if (gpu.attempted) {
        return;
    }
    gpu.attempted = true;
    gpu.state.version = VIRTIO_GPU_OUTPUT_ABI_VERSION;
    gpu.state.status = VIRTIO_GPU_OUTPUT_ABSENT;
    gpu.state.loader_framebuffer_selected = true;
    function = pci_find_device(GPU_VENDOR, GPU_DEVICE);
    if (function == NULL) {
        return;
    }
    gpu.state.status = VIRTIO_GPU_OUTPUT_FAILED;
    framebuffer = framebuffer_get_state();
    if (!framebuffer.active || framebuffer.width != width ||
        framebuffer.height != height || framebuffer.red_position != 16U ||
        framebuffer.green_position != 8U || framebuffer.blue_position != 0U ||
        width == 0U || height == 0U || width > GPU_MAX_DIMENSION ||
        height > GPU_MAX_DIMENSION ||
        (uint64_t)width * height * 4U >
            (uint64_t)GPU_MAX_PAGES * OPENRFS_PAGE_SIZE ||
        function->class_code != PCI_CLASS_DISPLAY ||
        !config32(function, PCI_REGISTER_VENDOR_ID, &identity) ||
        (uint16_t)identity != GPU_VENDOR ||
        (uint16_t)(identity >> 16U) != GPU_DEVICE) {
        return;
    }
    interrupts = cpu_interrupts_enabled();
    if (interrupts) {
        cpu_interrupt_disable();
    }
    const bool claimed = pci_claim_device(function, &gpu.claim) ==
        PCI_RESOURCE_STATUS_OK;
    const bool mapped = claimed &&
        pci_claim_update_command(&gpu.claim, PCI_COMMAND_INTX_DISABLE,
            PCI_COMMAND_INTX_DISABLE) == PCI_RESOURCE_STATUS_OK &&
        config32(function, PCI_REGISTER_COMMAND, &command_register) &&
        (command_register & PCI_COMMAND_INTX_DISABLE) != 0U &&
        message_interrupts_disabled(function) &&
        collect_regions(function) &&
        map_region(&gpu.common) && map_region(&gpu.notify) &&
        map_region(&gpu.device);
    if (interrupts) {
        cpu_interrupt_enable();
    }
    if (!mapped || !reset_device() || !negotiate()) {
        fail_output(1U);
        return;
    }
    for (size_t retry = 0U; retry < 8U; ++retry) {
        generation = mmio8(gpu.common.base, COMMON_GENERATION);
        scanouts = mmio32(gpu.device.base, DEVICE_SCANOUTS);
        if (generation == mmio8(gpu.common.base, COMMON_GENERATION)) {
            break;
        }
        if (retry == 7U) {
            scanouts = 0U;
        }
    }
    if (scanouts < 1U || scanouts > 16U || !allocate_dma(width, height)) {
        fail_output(2U);
        return;
    }
    interrupts = cpu_interrupts_enabled();
    if (interrupts) {
        cpu_interrupt_disable();
    }
    const bool queue_ready = configure_queue();
    master.allocations[0] = &gpu.queue;
    master.allocations[1] = &gpu.command;
    master.allocations[2] = &gpu.backing;
    master.allocation_count = 3U;
    const bool mastered = queue_ready && pci_claim_enable_bus_master(
        &gpu.claim, &master) == PCI_RESOURCE_STATUS_OK;
    if (interrupts) {
        cpu_interrupt_enable();
    }
    gpu.bus_master = mastered;
    if (!mastered) {
        fail_output(3U);
        return;
    }
    write8(gpu.common.base, COMMON_STATUS, 15U);
    if ((mmio8(gpu.common.base, COMMON_STATUS) & 4U) == 0U ||
        !scanout_accepts(width, height)) {
        fail_output(4U);
        return;
    }
    if (!setup_resource(width, height)) {
        fail_output(6U);
        return;
    }
    gpu.state.width = width;
    gpu.state.height = height;
    gpu.state.status = VIRTIO_GPU_OUTPUT_ACTIVE;
    gpu.state.loader_framebuffer_selected = false;
    console_serial_write("OpenRFS: VirtIO GPU 2D scanout configured; CPU-rendered pixels\n");
}

bool virtio_gpu_output_present(const struct surface *surface,
    struct surface_rect damage)
{
    uint8_t payload[32] = { 0 };
    uint8_t *target;
    const uint8_t *source;
    if (gpu.state.status != VIRTIO_GPU_OUTPUT_ACTIVE) {
        return false;
    }
    if (!device_still_present()) {
        ++gpu.state.dropped_frames;
        return false;
    }
    if (surface == NULL || !surface->active ||
        surface->width != gpu.state.width ||
        surface->height != gpu.state.height ||
        !virtio_gpu_rect_valid(damage.x, damage.y, damage.width,
            damage.height, surface->width, surface->height) ||
        (mmio8(gpu.common.base, COMMON_STATUS) & 64U) != 0U) {
        fail_output(9U);
        ++gpu.state.dropped_frames;
        return false;
    }
    if ((mmio32(gpu.device.base, 0U) & 1U) != 0U) {
        write32(gpu.device.base, 4U, 1U);
        if (!scanout_accepts(gpu.state.width, gpu.state.height)) {
            fail_output(12U);
            ++gpu.state.dropped_frames;
            return false;
        }
    }
    /* A fenced transfer has finished before backing returns to CPU ownership. */
    if (dma_transfer_to_cpu(&gpu.backing) != DMA_STATUS_OK) {
        fail_output(10U);
        ++gpu.state.dropped_frames;
        return false;
    }
    target = gpu.backing.cpu_address;
    source = (const uint8_t *)(const void *)surface->pixels;
    for (uint32_t row = 0U; row < damage.height; ++row) {
        const uint64_t from = (uint64_t)(damage.y + row) *
            surface->pitch + (uint64_t)damage.x * 4U;
        const uint64_t to = (uint64_t)(damage.y + row) *
            gpu.pitch + (uint64_t)damage.x * 4U;
        for (uint32_t byte = 0U; byte < damage.width * 4U; ++byte) {
            target[to + byte] = source[from + byte];
        }
    }
    gpu.state.copied_bytes += (uint64_t)damage.width * damage.height * 4U;
    cpu_store_fence();
    if (dma_transfer_to_device(&gpu.backing) != DMA_STATUS_OK) {
        fail_output(11U);
        ++gpu.state.dropped_frames;
        return false;
    }
    rectangle(payload, damage.x, damage.y, damage.width, damage.height);
    put64(payload + 16U, (uint64_t)damage.y * gpu.pitch +
        (uint64_t)damage.x * 4U);
    put32(payload + 24U, GPU_RESOURCE_ID);
    if (!simple_command(GPU_CMD_TRANSFER, payload, sizeof(payload))) {
        fail_output(7U);
        ++gpu.state.dropped_frames;
        return false;
    }
    clear(payload, sizeof(payload));
    rectangle(payload, damage.x, damage.y, damage.width, damage.height);
    put32(payload + 16U, GPU_RESOURCE_ID);
    if (!simple_command(GPU_CMD_FLUSH, payload, 24U)) {
        fail_output(8U);
        ++gpu.state.dropped_frames;
        return false;
    }
    ++gpu.state.frames_presented;
    return true;
}

struct virtio_gpu_output_state virtio_gpu_output_get_state(void)
{
    return gpu.state;
}

bool virtio_gpu_output_stop(void)
{
    if (gpu.state.status != VIRTIO_GPU_OUTPUT_ACTIVE) {
        return false;
    }
    fail_output(14U);
    if (gpu.claim.active || gpu.queue.active || gpu.command.active ||
        gpu.backing.active) {
        return false;
    }
    clear(&gpu, sizeof(gpu));
    return true;
}

void virtio_gpu_output_note_raster(uint64_t elapsed_ns)
{
    if (gpu.state.status == VIRTIO_GPU_OUTPUT_ACTIVE) {
        gpu.state.cpu_raster_ns += elapsed_ns;
    }
}
