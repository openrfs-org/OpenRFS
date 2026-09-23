/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The kernel-facing half of the SeaBIOS compatibility layer: one DMA arena
 * for every device the layer binds, a call stack inside that arena, PCI
 * claims through the driver framework, and block devices for the media the
 * drivers find. See include/openrfs/seabios_host.h for the boundary.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/blockdev.h>
#include <openrfs/console.h>
#include <openrfs/dma_arena.h>
#include <openrfs/hwdrv.h>
#include <openrfs/hwdrv_layers.h>
#include <openrfs/interrupts.h>
#include <openrfs/ioapic.h>
#include <openrfs/keyboard.h>
#include <openrfs/pci.h>
#include <openrfs/pci_resource.h>
#include <openrfs/pointer.h>
#include <openrfs/seabios_host.h>
#include <openrfs/thread.h>

/*
 * 4 MiB below 4 GiB. Command lists, rings and bounce buffers for every
 * controller bound at once, plus the call stack and the transfer buffer.
 */
#define SEABIOS_ARENA_PAGES 1024U
#define SEABIOS_ARENA_MAXIMUM_ADDRESS UINT64_C(0xFFFFFFFF)
/* SeaBIOS threads run on 4 KiB stacks; sixteen times that is ample. */
#define SEABIOS_STACK_BYTES (64U * 1024U)
/* block.c refuses transfers above 64 KiB, so no request needs more. */
#define SEABIOS_IO_BYTES (64U * 1024U)
#define SEABIOS_MAX_CLAIMS 16U
#define SEABIOS_MAX_MEDIA 16U
/*
 * The 8237 ISA DMA controller the floppy uses reaches only the first 16 MiB
 * and cannot cross a 64 KiB boundary. A small second arena below 16 MiB
 * holds one 64 KiB transfer buffer aligned to its size.
 */
#define SEABIOS_ISA_ARENA_PAGES 32U
#define SEABIOS_ISA_MAXIMUM_ADDRESS UINT64_C(0xFFFFFF)
#define SEABIOS_ISA_IRQS 16U
#define SEABIOS_REVISION "81ec9ec0bcf45df11fb7f98339ec9036b546fca0"

struct seabios_host_claim {
    struct hwdrv_pci_device device;
    size_t function_index;
    bool active;
};

struct seabios_host_medium_record {
    void *glue_drive;
    uint32_t block_size;
    char name[BLOCKDEV_NAME_CAPACITY];
    bool isa_dma;
    bool active;
};

static const struct hwdrv_origin seabios_origins[] = {
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/ahci.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/ata.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/virtio-blk.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/virtio-scsi.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/lsi-scsi.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/esp-scsi.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/megasas.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/mpt-scsi.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/pvscsi.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/sdcard.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/nvme.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/floppy.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/usb-xhci.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/usb-ehci.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/usb-uhci.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/usb-ohci.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/usb-hid.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/usb-msc.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/usb-uas.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEABIOS_REVISION, "src/hw/usb-hub.c", "LGPL-3.0-only" }
};

static struct dma_arena seabios_arena;
static struct seabios_host_claim claims[SEABIOS_MAX_CLAIMS];
static struct seabios_host_medium_record media[SEABIOS_MAX_MEDIA];
static uint8_t *call_stack;
static uint8_t *io_buffer;
static bool in_call;
static struct dma_arena isa_arena;
static uint8_t *isa_io_buffer;
static volatile uint32_t isa_irq_pending;
static uint32_t isa_irq_enabled;
static bool input_attached;
static uint32_t usb_host_count;
static uint32_t keyboard_count;
static uint32_t mouse_count;

static bool text_equal(const char *left, const char *right)
{
    size_t index = 0U;

    while (left[index] != '\0' && left[index] == right[index]) {
        ++index;
    }
    return left[index] == right[index];
}

static void copy_bytes(void *destination, const void *source, size_t length)
{
    uint8_t *to = destination;
    const uint8_t *from = source;

    for (size_t index = 0U; index < length; ++index) {
        to[index] = from[index];
    }
}

static const struct hwdrv_origin *origin_for(const char *path)
{
    for (size_t index = 0U;
         index < sizeof(seabios_origins) / sizeof(seabios_origins[0]);
         ++index) {
        if (text_equal(seabios_origins[index].path, path)) {
            return &seabios_origins[index];
        }
    }
    return NULL;
}

static bool isa_arena_ready(void);

static bool arena_ready(void)
{
    /*
     * Frames are handed out first-fit from the bottom of memory, so the ISA
     * buffer is reserved before the 4 MiB arena would take whatever is left
     * below 16 MiB. Only when the floppy driver may bind.
     */
    if (!seabios_arena.active && hwdrv_driver_enabled("floppy")) {
        (void)isa_arena_ready();
    }
    if (!seabios_arena.active &&
        dma_arena_create(&seabios_arena, SEABIOS_ARENA_PAGES,
            SEABIOS_ARENA_MAXIMUM_ADDRESS) != DMA_ARENA_STATUS_OK) {
        return false;
    }
    if (call_stack == NULL) {
        call_stack = dma_arena_allocate(&seabios_arena, SEABIOS_STACK_BYTES,
            4096U);
    }
    if (io_buffer == NULL) {
        /* Aligned to its size, so no transfer crosses a 64 KiB boundary. */
        io_buffer = dma_arena_allocate(&seabios_arena, SEABIOS_IO_BYTES,
            SEABIOS_IO_BYTES);
    }
    return call_stack != NULL && io_buffer != NULL;
}

/*
 * One trip into the vendored code: on the arena stack, with interrupts
 * disabled for its whole duration, and never re-entered.
 */
static bool seabios_call(struct seabios_call *call)
{
    uint64_t flags;
    bool preemptive;

    if (in_call || !arena_ready()) {
        return false;
    }
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    in_call = true;
    /* An interrupt window inside the call must never switch threads. */
    preemptive = thread_preemption_enabled();
    if (preemptive) {
        (void)thread_disable_preemption();
    }
    hwdrv_call_on_stack(seabios_glue_dispatch, call,
        call_stack + SEABIOS_STACK_BYTES);
    if (preemptive) {
        (void)thread_enable_preemption();
    }
    in_call = false;
    if ((flags & UINT64_C(0x200)) != 0U) {
        __asm__ volatile ("sti" : : : "memory");
    }
    return true;
}

static bool isa_arena_ready(void)
{
    if (!isa_arena.active &&
        dma_arena_create(&isa_arena, SEABIOS_ISA_ARENA_PAGES,
            SEABIOS_ISA_MAXIMUM_ADDRESS) != DMA_ARENA_STATUS_OK) {
        return false;
    }
    if (isa_io_buffer == NULL) {
        isa_io_buffer = dma_arena_allocate(&isa_arena, SEABIOS_IO_BYTES,
            SEABIOS_IO_BYTES);
    }
    return isa_io_buffer != NULL;
}

static void isa_interrupt(struct interrupt_frame *frame, void *context)
{
    (void)frame;
    isa_irq_pending |= UINT32_C(1) << (uint32_t)(uintptr_t)context;
}

/* ISA interrupts arrive on the I/O APIC's fixed vectors, one per IRQ. */
static uint8_t isa_vector(unsigned int irq)
{
    return (uint8_t)(INTERRUPT_IOAPIC_BASE + irq);
}

bool seabios_host_isa_irq_enable(unsigned int irq)
{
    if (irq >= SEABIOS_ISA_IRQS) {
        return false;
    }
    if ((isa_irq_enabled & (UINT32_C(1) << irq)) != 0U) {
        return true;
    }
    if (interrupt_register_handler(isa_vector(irq), isa_interrupt,
            (void *)(uintptr_t)irq) != INTERRUPT_STATUS_OK) {
        return false;
    }
    if (ioapic_route_isa_irq((uint8_t)irq, isa_vector(irq), 0U) !=
            IOAPIC_STATUS_OK) {
        (void)interrupt_unregister_handler(isa_vector(irq));
        return false;
    }
    isa_irq_enabled |= UINT32_C(1) << irq;
    return true;
}

void seabios_host_isa_irq_disable(unsigned int irq)
{
    if (irq >= SEABIOS_ISA_IRQS ||
        (isa_irq_enabled & (UINT32_C(1) << irq)) == 0U) {
        return;
    }
    (void)ioapic_mask_isa_irq((uint8_t)irq);
    (void)interrupt_unregister_handler(isa_vector(irq));
    isa_irq_enabled &= ~(UINT32_C(1) << irq);
}

uint32_t seabios_host_poll_irqs(void)
{
    uint32_t pending;

    if (isa_irq_enabled != 0U) {
        /* One instruction after sti is the earliest delivery point. */
        __asm__ volatile ("sti; nop; nop; nop; nop; cli" : : : "memory");
    }
    pending = isa_irq_pending;
    isa_irq_pending = 0U;
    return pending;
}

void *seabios_host_alloc(size_t size, size_t alignment)
{
    if (!arena_ready()) {
        return NULL;
    }
    return dma_arena_allocate(&seabios_arena, size, alignment);
}

void seabios_host_free(void *pointer)
{
    (void)dma_arena_free(&seabios_arena, pointer);
}

bool seabios_host_arena_contains(const void *pointer, size_t length)
{
    return dma_arena_contains(&seabios_arena, pointer, length);
}

void seabios_host_delay_ns(uint64_t nanoseconds)
{
    hwdrv_delay_ns(nanoseconds);
}

uint64_t seabios_host_now_ns(void)
{
    return hwdrv_now_ns();
}

void seabios_host_console_write(const char *text)
{
    console_serial_write(text);
}

_Noreturn void seabios_host_panic(const char *text)
{
    console_panic(text);
}

size_t seabios_host_pci_count(void)
{
    return pci_function_count();
}

bool seabios_host_pci_info(size_t index, struct seabios_host_pci_info *info)
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

bool seabios_host_function_read(size_t index, unsigned int offset,
    unsigned int width, uint32_t *value)
{
    const struct pci_function *function = pci_function_at(index);

    if (function == NULL || value == NULL || offset > UINT16_MAX) {
        return false;
    }
    return hwdrv_pci_function_read(function, (uint16_t)offset, width, value);
}

bool seabios_host_driver_enabled(const char *name)
{
    return hwdrv_driver_enabled(name);
}

bool seabios_host_native_driver_exists(
    const struct seabios_host_pci_info *info)
{
    /* src/kernel/nvme.c owns NVM Express controllers. */
    return info != NULL && info->class_code == PCI_CLASS_MASS_STORAGE &&
        info->subclass == PCI_SUBCLASS_NON_VOLATILE_MEMORY &&
        info->prog_if == PCI_PROG_IF_NVME;
}

void *seabios_host_claim(size_t index)
{
    const struct pci_function *function = pci_function_at(index);
    struct seabios_host_claim *claim = NULL;

    if (function == NULL || !arena_ready()) {
        return NULL;
    }
    for (size_t slot = 0U; slot < SEABIOS_MAX_CLAIMS; ++slot) {
        if (!claims[slot].active) {
            claim = &claims[slot];
            break;
        }
    }
    if (claim == NULL ||
        hwdrv_pci_claim(function, &claim->device) != HWDRV_STATUS_OK) {
        return NULL;
    }
    claim->device.arena = &seabios_arena;
    claim->function_index = index;
    claim->active = true;
    return claim;
}

void seabios_host_release(void *handle)
{
    struct seabios_host_claim *claim = handle;

    if (claim == NULL || !claim->active) {
        return;
    }
    (void)hwdrv_pci_release(&claim->device);
    claim->active = false;
}

bool seabios_host_bar(void *handle, unsigned int bar_index,
    struct seabios_host_bar *bar)
{
    struct seabios_host_claim *claim = handle;
    const struct pci_bar_description *description;

    if (claim == NULL || !claim->active || bar == NULL ||
        bar_index >= PCI_BAR_COUNT) {
        return false;
    }
    description = pci_claim_bar(&claim->device.claim, (uint8_t)bar_index);
    if (description == NULL) {
        return false;
    }
    bar->base = description->base;
    bar->size = description->size;
    bar->implemented = description->implemented;
    bar->io = description->kind == PCI_BAR_IO;
    bar->is_64_bit = description->kind == PCI_BAR_MEMORY_64;
    return true;
}

void *seabios_host_map_bar(void *handle, unsigned int bar_index)
{
    struct seabios_host_claim *claim = handle;
    volatile uint8_t *registers = NULL;
    uint64_t size = 0U;

    if (claim == NULL || !claim->active || bar_index >= PCI_BAR_COUNT ||
        hwdrv_pci_map_bar(&claim->device, (uint8_t)bar_index, &registers,
            &size) != HWDRV_STATUS_OK) {
        return NULL;
    }
    return (void *)(uintptr_t)registers;
}

bool seabios_host_enable_io(void *handle)
{
    struct seabios_host_claim *claim = handle;

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

bool seabios_host_enable_bus_master(void *handle)
{
    struct seabios_host_claim *claim = handle;

    if (claim == NULL || !claim->active) {
        return false;
    }
    return hwdrv_pci_enable_bus_master(&claim->device, &seabios_arena) ==
        HWDRV_STATUS_OK;
}

bool seabios_host_config_read(void *handle, unsigned int offset,
    unsigned int width, uint32_t *value)
{
    struct seabios_host_claim *claim = handle;

    if (claim == NULL || !claim->active || offset > UINT16_MAX) {
        return false;
    }
    return hwdrv_pci_config_read(&claim->device, (uint16_t)offset, width,
        value);
}

bool seabios_host_config_write(void *handle, unsigned int offset,
    unsigned int width, uint32_t value)
{
    struct seabios_host_claim *claim = handle;

    if (claim == NULL || !claim->active || offset > UINT16_MAX) {
        return false;
    }
    return hwdrv_pci_config_write(&claim->device, (uint16_t)offset, width,
        value);
}

/* Block operations: requests go through the arena transfer buffer. */
static enum blockdev_status medium_transfer(
    struct seabios_host_medium_record *record,
    enum seabios_call_kind kind,
    uint64_t lba,
    uint32_t count,
    uint8_t *buffer)
{
    const uint32_t chunk_blocks = SEABIOS_IO_BYTES / record->block_size;
    uint8_t *transfer = record->isa_dma ? isa_io_buffer : io_buffer;

    while (count > 0U) {
        const uint32_t blocks = count < chunk_blocks ? count : chunk_blocks;
        const size_t bytes = (size_t)blocks * record->block_size;
        struct seabios_call call = {
            .kind = kind,
            .glue_drive = record->glue_drive,
            .lba = lba,
            .count = blocks,
            .buffer = transfer,
            .result = -1
        };

        if (transfer == NULL) {
            return BLOCKDEV_STATUS_IO_ERROR;
        }
        if (kind == SEABIOS_CALL_WRITE) {
            copy_bytes(transfer, buffer, bytes);
        }
        if (!seabios_call(&call)) {
            return BLOCKDEV_STATUS_BUSY;
        }
        if (call.result != 0) {
            return BLOCKDEV_STATUS_IO_ERROR;
        }
        if (kind == SEABIOS_CALL_READ) {
            copy_bytes(buffer, transfer, bytes);
        }
        lba += blocks;
        count -= blocks;
        buffer += bytes;
    }
    return BLOCKDEV_STATUS_OK;
}

static enum blockdev_status medium_read(void *context, uint64_t lba,
    uint32_t count, void *buffer)
{
    return medium_transfer(context, SEABIOS_CALL_READ, lba, count, buffer);
}

static enum blockdev_status medium_write(void *context, uint64_t lba,
    uint32_t count, const void *buffer)
{
    return medium_transfer(context, SEABIOS_CALL_WRITE, lba, count,
        (uint8_t *)(uintptr_t)buffer);
}

static const struct blockdev_operations medium_operations = {
    .read = medium_read,
    .write = medium_write,
    .flush = NULL
};

static const struct blockdev_operations medium_read_only_operations = {
    .read = medium_read,
    .write = NULL,
    .flush = NULL
};

static enum blockdev_kind medium_kind(enum seabios_host_medium medium)
{
    switch (medium) {
    case SEABIOS_HOST_MEDIUM_OPTICAL:
        return BLOCKDEV_KIND_OPTICAL;
    case SEABIOS_HOST_MEDIUM_FLOPPY:
        return BLOCKDEV_KIND_FLOPPY;
    case SEABIOS_HOST_MEDIUM_FLASH:
        return BLOCKDEV_KIND_FLASH;
    case SEABIOS_HOST_MEDIUM_DISK:
    default:
        return BLOCKDEV_KIND_DISK;
    }
}

bool seabios_host_publish(void *handle, const struct seabios_host_drive *drive,
    char *instance, size_t instance_capacity)
{
    struct seabios_host_claim *claim = handle;
    struct seabios_host_medium_record *record = NULL;
    const struct hwdrv_origin *origin;
    struct blockdev_geometry geometry;
    enum blockdev_status status;

    if (drive == NULL || instance == NULL ||
        instance_capacity < BLOCKDEV_NAME_CAPACITY ||
        (claim != NULL && !claim->active)) {
        return false;
    }
    origin = origin_for(drive->source_path);
    for (size_t slot = 0U; slot < SEABIOS_MAX_MEDIA; ++slot) {
        if (!media[slot].active) {
            record = &media[slot];
            break;
        }
    }
    if (origin == NULL || record == NULL || drive->block_size == 0U ||
        drive->block_size > SEABIOS_IO_BYTES) {
        return false;
    }
    geometry.kind = medium_kind(drive->medium);
    geometry.block_size = drive->block_size;
    geometry.block_count = drive->block_count;
    geometry.read_only = drive->read_only;
    geometry.removable = drive->removable;
    record->glue_drive = drive->glue_drive;
    record->block_size = drive->block_size;
    record->isa_dma = drive->medium == SEABIOS_HOST_MEDIUM_FLOPPY;
    if (record->isa_dma && !isa_arena_ready()) {
        console_write("OpenRFS: SeaBIOS floppy needs ISA DMA memory below "
            "16 MiB and none is free\n");
        return false;
    }
    status = blockdev_register(drive->driver, drive->description, &geometry,
        drive->read_only ? &medium_read_only_operations : &medium_operations,
        record, record->name);
    if (status != BLOCKDEV_STATUS_OK) {
        console_write("OpenRFS: SeaBIOS ");
        console_write(drive->driver);
        console_write(" medium refused by the block layer: ");
        console_write(blockdev_status_string(status));
        console_putc('\n');
        return false;
    }
    record->active = true;
    for (size_t byte = 0U; byte < BLOCKDEV_NAME_CAPACITY; ++byte) {
        instance[byte] = record->name[byte];
    }
    if (hwdrv_record_binding(drive->driver, record->name, drive->description,
            origin, HWDRV_CLASS_STORAGE,
            claim != NULL ? claim->device.function : NULL) !=
        HWDRV_STATUS_OK) {
        return false;
    }
    console_write("OpenRFS: ");
    console_write(record->name);
    console_write(" bound by SeaBIOS ");
    console_write(drive->driver);
    console_write(": ");
    console_write(drive->description);
    console_putc('\n');
    return true;
}

static void format_instance(char *instance, size_t capacity,
    const char *prefix, uint32_t number)
{
    char digits[10];
    size_t used = 0U;
    size_t count = 0U;

    while (prefix[used] != '\0' && used + 1U < capacity) {
        instance[used] = prefix[used];
        ++used;
    }
    do {
        digits[count++] = (char)('0' + (char)(number % 10U));
        number /= 10U;
    } while (number != 0U && count < sizeof(digits));
    while (count > 0U && used + 1U < capacity) {
        instance[used++] = digits[--count];
    }
    instance[used] = '\0';
}

bool seabios_host_record(void *handle, enum seabios_host_device_class kind,
    const char *driver, const char *source_path, const char *prefix,
    const char *description, char *instance, size_t instance_capacity)
{
    struct seabios_host_claim *claim = handle;
    const struct hwdrv_origin *origin = origin_for(source_path);
    uint32_t *counter;

    if (driver == NULL || prefix == NULL || description == NULL ||
        instance == NULL || instance_capacity < HWDRV_INSTANCE_CAPACITY ||
        origin == NULL || (claim != NULL && !claim->active)) {
        return false;
    }
    if (kind == SEABIOS_HOST_DEVICE_USB_HOST) {
        counter = &usb_host_count;
    } else if (prefix[0] == 'k') {
        counter = &keyboard_count;
    } else {
        counter = &mouse_count;
    }
    format_instance(instance, instance_capacity, prefix, *counter);
    if (hwdrv_record_binding(driver, instance, description, origin,
            kind == SEABIOS_HOST_DEVICE_USB_HOST ? HWDRV_CLASS_USB_HOST :
                HWDRV_CLASS_INPUT,
            claim != NULL ? claim->device.function : NULL) !=
        HWDRV_STATUS_OK) {
        return false;
    }
    ++*counter;
    if (kind == SEABIOS_HOST_DEVICE_INPUT) {
        input_attached = true;
        hwdrv_note_input_device();
    }
    console_write("OpenRFS: ");
    console_write(instance);
    console_write(" bound by SeaBIOS ");
    console_write(driver);
    console_write(": ");
    console_write(description);
    console_putc('\n');
    return true;
}

void seabios_host_keyboard_byte(uint8_t scancode)
{
    (void)keyboard_submit_scancode(scancode);
}

void seabios_host_pointer_packet(uint8_t flags, uint8_t delta_x,
    uint8_t delta_y)
{
    (void)pointer_submit_packet(flags, delta_x, delta_y);
}

void seabios_layer_poll_input(void)
{
    struct seabios_call call = {
        .kind = SEABIOS_CALL_POLL_INPUT,
        .result = 0
    };

    if (input_attached && !in_call) {
        (void)seabios_call(&call);
    }
}

size_t seabios_layer_driver_count(void)
{
    return seabios_glue_driver_count();
}

const char *seabios_layer_driver_name(size_t index)
{
    return seabios_glue_driver_name(index);
}

/* Bind one PCI function if a driver of this pass matches it. */
static size_t bind_function(size_t index, int pass)
{
    const struct pci_function *function = pci_function_at(index);
    struct seabios_host_pci_info info;
    struct seabios_call call;
    const char *name;
    int driver;

    if (function == NULL ||
        function->header_type != PCI_HEADER_TYPE_ENDPOINT ||
        hwdrv_pci_function_claimed(function) ||
        !seabios_host_pci_info(index, &info)) {
        return 0U;
    }
    driver = seabios_glue_match(&info);
    if (driver < 0 || seabios_glue_bind_pass((size_t)driver) != pass) {
        return 0U;
    }
    name = seabios_glue_driver_name((size_t)driver);
    if (!hwdrv_driver_enabled(name) ||
        (seabios_host_native_driver_exists(&info) &&
            hwdrv_get_mode() != HWDRV_MODE_SELECTED)) {
        return 0U;
    }
    call = (struct seabios_call){
        .kind = SEABIOS_CALL_BIND_PCI,
        .function_index = index,
        .info = &info,
        .result = 0
    };
    if (!seabios_call(&call)) {
        hwdrv_record_probe_failure();
        return 0U;
    }
    if (call.result <= 0) {
        console_write("OpenRFS: SeaBIOS ");
        console_write(name);
        console_write(" found no devices\n");
        return 0U;
    }
    return (size_t)call.result;
}

enum hwdrv_status seabios_layer_bind_all(void)
{
    size_t published = 0U;

    for (int pass = 0; pass < SEABIOS_BIND_PASSES; ++pass) {
        for (size_t index = 0U; index < pci_function_count(); ++index) {
            published += bind_function(index, pass);
        }
    }
    /* The floppy controller is an ISA device at fixed ports. */
    if (hwdrv_driver_enabled("floppy")) {
        struct seabios_call call = {
            .kind = SEABIOS_CALL_BIND_ISA,
            .result = 0
        };

        if (seabios_call(&call) && call.result > 0) {
            published += (size_t)call.result;
        }
    }
    return published != 0U ? HWDRV_STATUS_OK : HWDRV_STATUS_ABSENT;
}
