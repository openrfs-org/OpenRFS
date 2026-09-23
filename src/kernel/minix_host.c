/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The kernel-facing half of the MINIX 3 audio drivers: which driver drives
 * which card, the claim and the DMA arena a card may master, and the PCM
 * device through which the kernel plays sound. See
 * include/openrfs/minix_host.h for the boundary.
 *
 * The ES1370 is a PCI function matched by its IDs and binds under
 * openrfs.drivers=auto or when named. The Sound Blaster 16 is an ISA card
 * the driver finds by resetting its DSP at the fixed base 0x220, so it
 * binds only when named: probing I/O ports nobody described is not
 * something to do to an arbitrary machine.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/clock.h>
#include <openrfs/console.h>
#include <openrfs/dma_arena.h>
#include <openrfs/hwdrv.h>
#include <openrfs/hwdrv_layers.h>
#include <openrfs/minix_host.h>
#include <openrfs/pci.h>
#include <openrfs/pcm.h>

#define MINIX_REVISION "4db99f4012570a577414fe2a43697b2f239b699e"
#define MINIX_MAX_DEVICES 2U
/*
 * One 64 KiB DMA buffer aligned to its size, 128 KiB of extra buffers
 * (four 32 KiB fragments) and the alignment slack between them.
 */
#define MINIX_ARENA_PAGES 64U
#define MINIX_PCI_ARENA_LIMIT UINT64_C(0xFFFFFFFF)
/* The 8237 DMA controller an ISA card uses reaches the first 16 MiB. */
#define MINIX_ISA_ARENA_LIMIT UINT64_C(0xFFFFFF)
/* A device that services no fragment in this long has stopped. */
#define MINIX_STALL_NS UINT64_C(2000000000)

struct minix_variant {
    const char *name;
    const char *description;
    void (*dispatch)(struct minix_audio_call *call);
    const struct hwdrv_origin *origin;
    bool pci;
    uint16_t vendor_id;
    uint16_t device_id;
};

struct minix_device {
    const struct minix_variant *variant;
    struct hwdrv_pci_device device;
    bool claimed;
    struct dma_arena arena;
    char name[PCM_NAME_CAPACITY];
    uint32_t fragment_bytes;
    struct minix_audio_call state;
    bool active;
};

static const struct hwdrv_origin minix_origins[] = {
    { "MINIX 3", MINIX_REVISION, "minix/drivers/audio/es1370/es1370.c",
        "BSD-3-Clause (MINIX 3 licence)" },
    { "MINIX 3", MINIX_REVISION, "minix/drivers/audio/sb16/sb16.c",
        "BSD-3-Clause (MINIX 3 licence)" }
};

static const struct minix_variant variants[] = {
    { "es1370", "Ensoniq AudioPCI ES1370", minix_es1370_dispatch,
        &minix_origins[0], true, UINT16_C(0x1274), UINT16_C(0x5000) },
    { "sb16", "Sound Blaster 16 (ISA, 0x220)", minix_sb16_dispatch,
        &minix_origins[1], false, 0U, 0U }
};

#define MINIX_VARIANT_COUNT (sizeof(variants) / sizeof(variants[0]))

static struct minix_device devices[MINIX_MAX_DEVICES];

void minix_host_console_write(const char *text)
{
    console_write(text);
}

_Noreturn void minix_host_panic(const char *text)
{
    console_panic(text);
}

bool minix_host_config_read(void *handle, unsigned int offset,
    unsigned int width, uint32_t *value)
{
    struct minix_device *device = handle;

    if (device == NULL || !device->claimed || value == NULL ||
        offset > UINT16_MAX) {
        return false;
    }
    return hwdrv_pci_config_read(&device->device, (uint16_t)offset, width,
        value);
}

bool minix_host_config_write(void *handle, unsigned int offset,
    unsigned int width, uint32_t value)
{
    struct minix_device *device = handle;

    if (device == NULL || !device->claimed || offset > UINT16_MAX) {
        return false;
    }
    return hwdrv_pci_config_write(&device->device, (uint16_t)offset, width,
        value);
}

void *minix_host_alloc(void *handle, size_t size, size_t alignment)
{
    struct minix_device *device = handle;

    if (device == NULL || !device->arena.active) {
        return NULL;
    }
    return dma_arena_allocate(&device->arena, size, alignment);
}

size_t minix_layer_driver_count(void)
{
    return MINIX_VARIANT_COUNT;
}

const char *minix_layer_driver_name(size_t index)
{
    return index < MINIX_VARIANT_COUNT ? variants[index].name : NULL;
}

static bool call_driver(struct minix_device *device,
    struct minix_audio_call *call)
{
    call->handle = device;
    device->variant->dispatch(call);
    device->state = *call;
    return call->result == 0;
}

static bool service(struct minix_device *device)
{
    struct minix_audio_call call = { .kind = MINIX_AUDIO_CALL_SERVICE };

    return call_driver(device, &call);
}

static enum pcm_status device_open(void *context,
    const struct pcm_format *format, uint32_t *fragment_bytes)
{
    struct minix_device *device = context;
    struct minix_audio_call call = { .kind = MINIX_AUDIO_CALL_OPEN };
    const struct {
        enum minix_audio_setting setting;
        uint32_t value;
    } settings[] = {
        { MINIX_AUDIO_SET_BITS, format->bits },
        { MINIX_AUDIO_SET_STEREO, format->channels == 2U ? 1U : 0U },
        { MINIX_AUDIO_SET_SIGN, format->is_signed ? 1U : 0U },
        { MINIX_AUDIO_SET_RATE, format->rate }
    };

    if (format->channels != 1U && format->channels != 2U) {
        return PCM_STATUS_UNSUPPORTED;
    }
    if (!call_driver(device, &call)) {
        return PCM_STATUS_DEVICE_ERROR;
    }
    /* The DSPIO* requests a MINIX program issues after open(). */
    for (size_t index = 0U; index < sizeof(settings) / sizeof(settings[0]);
         ++index) {
        call = (struct minix_audio_call){
            .kind = MINIX_AUDIO_CALL_CONFIGURE,
            .setting = settings[index].setting,
            .value = settings[index].value
        };
        if (!call_driver(device, &call)) {
            struct minix_audio_call close = {
                .kind = MINIX_AUDIO_CALL_CLOSE
            };

            (void)call_driver(device, &close);
            return PCM_STATUS_UNSUPPORTED;
        }
    }
    device->fragment_bytes = device->state.fragment_bytes;
    *fragment_bytes = device->fragment_bytes;
    return PCM_STATUS_OK;
}

static enum pcm_status device_write(void *context, const void *fragment,
    uint64_t timeout_ns)
{
    struct minix_device *device = context;
    const uint64_t deadline = clock_monotonic_ns() + timeout_ns;

    for (;;) {
        struct minix_audio_call call = {
            .kind = MINIX_AUDIO_CALL_WRITE,
            .fragment = fragment
        };

        if (!call_driver(device, &call)) {
            return PCM_STATUS_DEVICE_ERROR;
        }
        if (call.accepted) {
            return PCM_STATUS_OK;
        }
        /* Both buffers full: let finished fragments make room. */
        if (!service(device)) {
            return PCM_STATUS_DEVICE_ERROR;
        }
        if (clock_monotonic_ns() >= deadline) {
            return PCM_STATUS_TIMEOUT;
        }
        __asm__ volatile ("pause" : : : "memory");
    }
}

static enum pcm_status device_drain(void *context, uint64_t timeout_ns)
{
    struct minix_device *device = context;
    const uint64_t deadline = clock_monotonic_ns() + timeout_ns;
    uint64_t progress = clock_monotonic_ns();
    uint64_t interrupts = device->state.interrupts;

    while (!device->state.out_of_data ||
        device->state.dma_fragments_queued != 0U ||
        device->state.extra_fragments_queued != 0U) {
        const uint64_t now = clock_monotonic_ns();

        if (!service(device)) {
            return PCM_STATUS_DEVICE_ERROR;
        }
        if (device->state.interrupts != interrupts) {
            interrupts = device->state.interrupts;
            progress = now;
        }
        if (now >= deadline || now - progress > MINIX_STALL_NS) {
            return PCM_STATUS_TIMEOUT;
        }
        __asm__ volatile ("pause" : : : "memory");
    }
    return PCM_STATUS_OK;
}

static enum pcm_status device_close(void *context)
{
    struct minix_device *device = context;
    struct minix_audio_call call = { .kind = MINIX_AUDIO_CALL_CLOSE };

    return call_driver(device, &call) ? PCM_STATUS_OK :
        PCM_STATUS_DEVICE_ERROR;
}

static void device_statistics(void *context,
    struct pcm_statistics *statistics)
{
    const struct minix_device *device = context;

    statistics->fragments_written = device->state.fragments_written;
    statistics->interrupts = device->state.interrupts;
    statistics->pauses = device->state.pauses;
}

static const struct pcm_operations device_operations = {
    .open = device_open,
    .write = device_write,
    .drain = device_drain,
    .close = device_close,
    .statistics = device_statistics
};

static void write_decimal(uint64_t value)
{
    char digits[21];
    size_t count = 0U;

    do {
        digits[count++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count > 0U) {
        console_putc(digits[--count]);
    }
}

static void release(struct minix_device *device)
{
    if (device->claimed) {
        (void)hwdrv_pci_release(&device->device);
        device->claimed = false;
    }
    if (device->arena.active) {
        (void)dma_arena_destroy(&device->arena);
    }
}

/* Probe one card through its driver and publish it as a PCM device. */
static bool bind_device(const struct minix_variant *variant,
    const struct pci_function *function)
{
    struct minix_device *device = NULL;
    struct minix_audio_call call = { .kind = MINIX_AUDIO_CALL_PROBE };

    for (size_t slot = 0U; slot < MINIX_MAX_DEVICES; ++slot) {
        if (!devices[slot].active && !devices[slot].claimed &&
            !devices[slot].arena.active) {
            device = &devices[slot];
            break;
        }
    }
    if (device == NULL) {
        return false;
    }
    device->variant = variant;
    if (dma_arena_create(&device->arena, MINIX_ARENA_PAGES,
            variant->pci ? MINIX_PCI_ARENA_LIMIT : MINIX_ISA_ARENA_LIMIT) !=
        DMA_ARENA_STATUS_OK) {
        console_write("OpenRFS: MINIX ");
        console_write(variant->name);
        console_write(" has no DMA memory\n");
        return false;
    }
    if (function != NULL) {
        if (hwdrv_pci_claim(function, &device->device) != HWDRV_STATUS_OK) {
            release(device);
            return false;
        }
        device->claimed = true;
        /* The only memory the card may master: its own arena. */
        device->device.arena = &device->arena;
    }
    if (!call_driver(device, &call)) {
        console_write("OpenRFS: MINIX ");
        console_write(variant->name);
        console_write(" found no device\n");
        release(device);
        return false;
    }
    if (pcm_register(variant->name, variant->description, &device_operations,
            device, device->name) != PCM_STATUS_OK) {
        release(device);
        return false;
    }
    device->active = true;
    if (hwdrv_record_binding(variant->name, device->name,
            variant->description, variant->origin, HWDRV_CLASS_AUDIO,
            function) != HWDRV_STATUS_OK) {
        return false;
    }
    console_write("OpenRFS: ");
    console_write(device->name);
    console_write(" bound by MINIX ");
    console_write(variant->name);
    console_write(": ");
    console_write(device->state.driver_name != NULL ?
        device->state.driver_name : variant->description);
    console_write(", IRQ ");
    write_decimal((uint64_t)(uint32_t)device->state.irq);
    console_write(" (serviced by polling its interrupt status)\n");
    return true;
}

enum hwdrv_status minix_layer_bind_all(void)
{
    size_t bound = 0U;

    for (size_t index = 0U; index < pci_function_count(); ++index) {
        const struct pci_function *function = pci_function_at(index);

        if (function == NULL ||
            function->header_type != PCI_HEADER_TYPE_ENDPOINT ||
            hwdrv_pci_function_claimed(function)) {
            continue;
        }
        for (size_t slot = 0U; slot < MINIX_VARIANT_COUNT; ++slot) {
            const struct minix_variant *variant = &variants[slot];

            if (variant->pci && function->vendor_id == variant->vendor_id &&
                function->device_id == variant->device_id &&
                hwdrv_driver_enabled(variant->name) &&
                bind_device(variant, function)) {
                ++bound;
            }
        }
    }
    for (size_t slot = 0U; slot < MINIX_VARIANT_COUNT; ++slot) {
        const struct minix_variant *variant = &variants[slot];

        if (!variant->pci && hwdrv_get_mode() == HWDRV_MODE_SELECTED &&
            hwdrv_driver_enabled(variant->name) &&
            bind_device(variant, NULL)) {
            ++bound;
        }
    }
    return bound != 0U ? HWDRV_STATUS_OK : HWDRV_STATUS_ABSENT;
}
