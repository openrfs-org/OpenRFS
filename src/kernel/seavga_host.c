/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The kernel-facing half of the SeaBIOS VGA drivers: which card-type build
 * drives which adapter, the PCI claim and BAR mappings the adapter needs,
 * and the display registry entry through which the kernel sets a mode. See
 * include/openrfs/seavga_host.h for the boundary.
 *
 * Display drivers bind only when named on the command line
 * (openrfs.drivers=bochsvga, ...), never under openrfs.drivers=auto. The
 * primary adapter is the one the loader set a mode on and the kernel's
 * screen console draws to; a driver that takes it over must be asked for.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/console.h>
#include <openrfs/display.h>
#include <openrfs/dma_arena.h>
#include <openrfs/hwdrv.h>
#include <openrfs/hwdrv_layers.h>
#include <openrfs/pci.h>
#include <openrfs/pci_resource.h>
#include <openrfs/seabios_host.h>
#include <openrfs/seavga_host.h>

#define SEAVGA_REVISION "81ec9ec0bcf45df11fb7f98339ec9036b546fca0"
#define SEAVGA_MAX_ADAPTERS 4U
#define SEAVGA_PCI_SUBCLASS_VGA UINT8_C(0x00)
#define SEAVGA_PCI_SUBCLASS_OTHER UINT8_C(0x80)
/* The VGA memory windows at A0000-BFFFF that legacy modes scan out of. */
#define SEAVGA_LEGACY_WINDOW_BASE UINT64_C(0xA0000)
#define SEAVGA_LEGACY_WINDOW_END UINT64_C(0xC0000)
/* Register windows (EDID, DISPI, MMIO) are small; framebuffers are not. */
#define SEAVGA_REGISTER_BAR_LIMIT (UINT64_C(1) << 20)

struct seavga_variant {
    const char *name;
    const char *description;
    void (*dispatch)(void *call);
    bool (*match)(const struct pci_function *function);
    const struct hwdrv_origin *origin;
    /* The build programs the legacy VGA registers at 3C0-3DF (and 1CE). */
    bool legacy_ports;
};

struct seavga_adapter {
    const struct seavga_variant *variant;
    struct hwdrv_pci_device device;
    bool claimed;
    int bdf;
    /* The BAR holding the linear framebuffer, once mapped. */
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    volatile uint8_t *framebuffer_pixels;
    char name[DISPLAY_NAME_CAPACITY];
    bool active;
};

static const struct hwdrv_origin seavga_origins[] = {
    { "SeaBIOS", SEAVGA_REVISION, "vgasrc/stdvga.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEAVGA_REVISION, "vgasrc/bochsvga.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEAVGA_REVISION, "vgasrc/clext.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEAVGA_REVISION, "vgasrc/atiext.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEAVGA_REVISION, "vgasrc/bochsdisplay.c", "LGPL-3.0-only" },
    { "SeaBIOS", SEAVGA_REVISION, "vgasrc/ramfb.c", "LGPL-3.0-only" }
};

static bool is_vga(const struct pci_function *function)
{
    return function->class_code == PCI_CLASS_DISPLAY &&
        function->subclass == SEAVGA_PCI_SUBCLASS_VGA;
}

static bool match_cirrus(const struct pci_function *function)
{
    /* QEMU's cirrus-vga, a CL-GD5446, the card vgabios-cirrus.bin serves. */
    return function->vendor_id == UINT16_C(0x1013) &&
        function->device_id == UINT16_C(0x00B8);
}

static bool match_ati(const struct pci_function *function)
{
    /* Rage 128 Pro and Radeon RV100, the two models QEMU's ati-vga has. */
    return function->vendor_id == UINT16_C(0x1002) &&
        (function->device_id == UINT16_C(0x5046) ||
            function->device_id == UINT16_C(0x5159));
}

static bool match_bochsvga(const struct pci_function *function)
{
    /*
     * The adapters SeaBIOS builds bochsvga.c for: QEMU's standard VGA, and
     * the VMware SVGA, QXL and virtio-vga cards that carry the same VBE
     * DISPI interface. bochsvga_setup() picks the framebuffer BAR by vendor.
     */
    const uint16_t vendor = function->vendor_id;
    const uint16_t device = function->device_id;

    return is_vga(function) &&
        ((vendor == UINT16_C(0x1234) && device == UINT16_C(0x1111)) ||
            (vendor == UINT16_C(0x15AD) && device == UINT16_C(0x0405)) ||
            (vendor == UINT16_C(0x1B36) && device == UINT16_C(0x0100)) ||
            (vendor == UINT16_C(0x1AF4) && device == UINT16_C(0x1050)));
}

static bool match_bochs_display(const struct pci_function *function)
{
    /* QEMU's bochs-display: the DISPI interface without legacy VGA. */
    return function->vendor_id == UINT16_C(0x1234) &&
        function->device_id == UINT16_C(0x1111) &&
        function->class_code == PCI_CLASS_DISPLAY &&
        function->subclass == SEAVGA_PCI_SUBCLASS_OTHER;
}

static bool match_stdvga(const struct pci_function *function)
{
    /* Any VGA-compatible adapter answers the standard VGA registers. */
    return is_vga(function) && function->prog_if == 0U;
}

/*
 * In binding order: a card-specific build before the standard VGA build
 * that would also match the same adapter. ramfb has no PCI function.
 */
static const struct seavga_variant variants[] = {
    { "cirrus", "Cirrus Logic GD5446", seavga_cirrus_dispatch,
        match_cirrus, &seavga_origins[2], true },
    { "ati", "ATI Rage 128 / Radeon", seavga_ati_dispatch, match_ati,
        &seavga_origins[3], true },
    { "bochsvga", "Bochs VBE DISPI VGA", seavga_bochsvga_dispatch,
        match_bochsvga, &seavga_origins[1], true },
    { "bochs-display", "Bochs display", seavga_bochs_display_dispatch,
        match_bochs_display, &seavga_origins[4], false },
    { "stdvga", "standard VGA", seavga_stdvga_dispatch, match_stdvga,
        &seavga_origins[0], true },
    { "ramfb", "QEMU RAM framebuffer", seavga_ramfb_dispatch, NULL,
        &seavga_origins[5], false }
};

#define SEAVGA_VARIANT_COUNT (sizeof(variants) / sizeof(variants[0]))

static struct seavga_adapter adapters[SEAVGA_MAX_ADAPTERS];
static bool variant_bound[SEAVGA_VARIANT_COUNT];
static struct dma_arena framebuffer_arena;
static uint8_t *ram_framebuffer;
static uint64_t ram_framebuffer_size;

static void write_hex(uint64_t value, unsigned int digits)
{
    static const char hex[] = "0123456789abcdef";
    char text[17];

    if (digits > 16U) {
        digits = 16U;
    }
    for (unsigned int index = 0U; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1U - index) * 4U)) & 0xFU];
    }
    text[digits] = '\0';
    console_write(text);
}

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

static void append_text(char *buffer, size_t capacity, size_t *used,
    const char *text)
{
    while (*text != '\0' && *used + 1U < capacity) {
        buffer[(*used)++] = *text++;
    }
    buffer[*used] = '\0';
}

static void append_hex(char *buffer, size_t capacity, size_t *used,
    uint64_t value, unsigned int digits)
{
    static const char hex[] = "0123456789abcdef";

    for (unsigned int index = 0U; index < digits; ++index) {
        const char text[2] = {
            hex[(value >> ((digits - 1U - index) * 4U)) & 0xFU], '\0'
        };
        append_text(buffer, capacity, used, text);
    }
}

static void append_decimal(char *buffer, size_t capacity, size_t *used,
    uint64_t value)
{
    char digits[21];
    size_t count = 0U;

    do {
        digits[count++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count > 0U) {
        const char text[2] = { digits[--count], '\0' };
        append_text(buffer, capacity, used, text);
    }
}

bool seavga_host_config_read(void *handle, unsigned int offset,
    unsigned int width, uint32_t *value)
{
    struct seavga_adapter *adapter = handle;

    if (adapter == NULL || !adapter->claimed || offset > UINT16_MAX ||
        value == NULL) {
        return false;
    }
    return hwdrv_pci_config_read(&adapter->device, (uint16_t)offset, width,
        value);
}

bool seavga_host_config_write(void *handle, unsigned int offset,
    unsigned int width, uint32_t value)
{
    struct seavga_adapter *adapter = handle;

    if (adapter == NULL || !adapter->claimed || offset > UINT16_MAX) {
        return false;
    }
    return hwdrv_pci_config_write(&adapter->device, (uint16_t)offset, width,
        value);
}

bool seavga_host_bar(void *handle, unsigned int bar_index, uint64_t *base,
    uint64_t *size, bool *io)
{
    struct seavga_adapter *adapter = handle;
    const struct pci_bar_description *bar;

    if (adapter == NULL || !adapter->claimed || base == NULL ||
        size == NULL || io == NULL || bar_index >= PCI_BAR_COUNT) {
        return false;
    }
    bar = pci_claim_bar(&adapter->device.claim, (uint8_t)bar_index);
    if (bar == NULL || !bar->implemented) {
        return false;
    }
    *base = bar->base;
    *size = bar->size;
    *io = bar->kind == PCI_BAR_IO;
    return true;
}

uint32_t seavga_host_allocate_framebuffer(uint32_t size)
{
    const size_t pages = ((size_t)size + 4095U) / 4096U;

    /* One RAM framebuffer, for the one ramfb build. */
    if (size == 0U || ram_framebuffer != NULL ||
        dma_arena_create(&framebuffer_arena, pages, UINT64_C(0xFFFFFFFF)) !=
            DMA_ARENA_STATUS_OK) {
        return 0U;
    }
    ram_framebuffer = dma_arena_allocate(&framebuffer_arena,
        (uint64_t)pages * 4096U, 4096U);
    if (ram_framebuffer == NULL) {
        return 0U;
    }
    ram_framebuffer_size = (uint64_t)pages * 4096U;
    return (uint32_t)(uintptr_t)ram_framebuffer;
}

size_t seavga_layer_driver_count(void)
{
    return SEAVGA_VARIANT_COUNT;
}

const char *seavga_layer_driver_name(size_t index)
{
    return index < SEAVGA_VARIANT_COUNT ? variants[index].name : NULL;
}

static enum display_status adapter_set_mode(void *context, uint32_t width,
    uint32_t height, uint32_t bits_per_pixel, struct display_mode *mode)
{
    struct seavga_adapter *adapter = context;
    struct seavga_call call = {
        .kind = SEAVGA_CALL_SET_MODE,
        .handle = adapter->claimed ? adapter : NULL,
        .bdf = adapter->bdf,
        .width = width,
        .height = height,
        .bits_per_pixel = bits_per_pixel,
        .result = -1
    };
    uint64_t bytes;
    volatile uint8_t *pixels = NULL;

    if (!seabios_host_run(adapter->variant->dispatch, &call)) {
        return DISPLAY_STATUS_BUSY;
    }
    if (call.result == SEAVGA_RESULT_NO_SUCH_MODE) {
        return DISPLAY_STATUS_NO_SUCH_MODE;
    }
    if (call.result != 0) {
        console_write("OpenRFS: SeaBIOS ");
        console_write(adapter->variant->name);
        console_write(" refused the mode (");
        console_write(call.result < 0 ? "-" : "");
        write_decimal(call.result < 0 ? (uint64_t)-(int64_t)call.result :
            (uint64_t)call.result);
        console_write(")\n");
        return DISPLAY_STATUS_DEVICE_ERROR;
    }
    bytes = (uint64_t)call.pitch * height;
    if (call.linear && ram_framebuffer != NULL && !adapter->claimed) {
        /* ramfb: the framebuffer is RAM this host allocated. */
        if (call.framebuffer == (uint64_t)(uintptr_t)ram_framebuffer &&
            bytes <= ram_framebuffer_size) {
            pixels = ram_framebuffer;
        }
    } else if (call.linear) {
        if (adapter->framebuffer_pixels != NULL &&
            call.framebuffer >= adapter->framebuffer_base &&
            call.framebuffer - adapter->framebuffer_base <=
                adapter->framebuffer_size &&
            bytes <= adapter->framebuffer_size -
                (call.framebuffer - adapter->framebuffer_base)) {
            pixels = adapter->framebuffer_pixels +
                (call.framebuffer - adapter->framebuffer_base);
        }
    } else if (call.framebuffer >= SEAVGA_LEGACY_WINDOW_BASE &&
        call.framebuffer < SEAVGA_LEGACY_WINDOW_END &&
        bytes <= SEAVGA_LEGACY_WINDOW_END - call.framebuffer) {
        /* The identity map covers the legacy VGA window. */
        pixels = (volatile uint8_t *)(uintptr_t)call.framebuffer;
    }
    if (pixels == NULL) {
        console_write("OpenRFS: SeaBIOS ");
        console_write(adapter->variant->name);
        console_write(" reported a framebuffer outside its mapping\n");
        return DISPLAY_STATUS_DEVICE_ERROR;
    }
    mode->width = width;
    mode->height = height;
    mode->bits_per_pixel = bits_per_pixel;
    mode->pitch = call.pitch;
    mode->framebuffer = call.framebuffer;
    mode->pixels = pixels;
    mode->framebuffer_bytes = bytes;
    mode->mode_number = call.mode_number;
    /* SeaBIOS's MM_PACKED: indexed colour through the DAC. */
    mode->palette = call.memory_model == 4U;
    return DISPLAY_STATUS_OK;
}

static const struct display_operations adapter_operations = {
    .set_mode = adapter_set_mode
};

/* Map every memory BAR the build reads through physical addresses. */
static bool map_register_bars(struct seavga_adapter *adapter)
{
    for (uint8_t index = 0U; index < PCI_BAR_COUNT; ++index) {
        const struct pci_bar_description *bar =
            pci_claim_bar(&adapter->device.claim, index);
        volatile uint8_t *registers = NULL;
        uint16_t port = 0U;

        if (bar == NULL || !bar->implemented) {
            continue;
        }
        if (bar->kind == PCI_BAR_IO) {
            if (hwdrv_pci_io_bar(&adapter->device, index, &port, NULL) !=
                    HWDRV_STATUS_OK) {
                return false;
            }
            continue;
        }
        if (bar->size <= SEAVGA_REGISTER_BAR_LIMIT &&
            hwdrv_pci_map_bar(&adapter->device, index, &registers, NULL) !=
                HWDRV_STATUS_OK) {
            return false;
        }
    }
    return true;
}

/* Map the BAR the driver reported its linear framebuffer in. */
static bool map_framebuffer_bar(struct seavga_adapter *adapter,
    uint64_t framebuffer)
{
    for (uint8_t index = 0U; index < PCI_BAR_COUNT; ++index) {
        const struct pci_bar_description *bar =
            pci_claim_bar(&adapter->device.claim, index);
        volatile uint8_t *pixels = NULL;
        uint64_t size = 0U;

        if (bar == NULL || !bar->implemented || bar->kind == PCI_BAR_IO ||
            framebuffer < bar->base || framebuffer - bar->base >= bar->size) {
            continue;
        }
        if (hwdrv_pci_map_bar(&adapter->device, index, &pixels, &size) !=
                HWDRV_STATUS_OK) {
            return false;
        }
        adapter->framebuffer_base = bar->base;
        adapter->framebuffer_size = size;
        adapter->framebuffer_pixels = pixels;
        return true;
    }
    return false;
}

static struct seavga_adapter *free_adapter(void)
{
    for (size_t slot = 0U; slot < SEAVGA_MAX_ADAPTERS; ++slot) {
        if (!adapters[slot].active && !adapters[slot].claimed) {
            return &adapters[slot];
        }
    }
    return NULL;
}

static void report_refusal(const struct seavga_variant *variant,
    const char *reason)
{
    console_write("OpenRFS: SeaBIOS ");
    console_write(variant->name);
    console_write(" not bound: ");
    console_write(reason);
    console_putc('\n');
}

/* Register the adapter a build just set up, and record the binding. */
static bool publish_adapter(struct seavga_adapter *adapter,
    const struct seavga_call *call, const struct pci_function *function)
{
    const struct seavga_variant *variant = adapter->variant;
    char description[DISPLAY_DESCRIPTION_CAPACITY];
    size_t used = 0U;

    description[0] = '\0';
    append_text(description, sizeof(description), &used,
        variant->description);
    if (function != NULL) {
        append_text(description, sizeof(description), &used, " ");
        append_hex(description, sizeof(description), &used,
            function->vendor_id, 4U);
        append_text(description, sizeof(description), &used, ":");
        append_hex(description, sizeof(description), &used,
            function->device_id, 4U);
    }
    if (call->total_memory != 0U) {
        append_text(description, sizeof(description), &used, ", ");
        append_decimal(description, sizeof(description), &used,
            call->total_memory / 1024U);
        append_text(description, sizeof(description), &used, " KiB");
    }
    if (display_register(variant->name, description, &adapter_operations,
            adapter, adapter->name) != DISPLAY_STATUS_OK) {
        report_refusal(variant, "the display table is full");
        return false;
    }
    adapter->active = true;
    if (hwdrv_record_binding(variant->name, adapter->name, description,
            variant->origin, HWDRV_CLASS_DISPLAY, function) !=
        HWDRV_STATUS_OK) {
        return false;
    }
    console_write("OpenRFS: ");
    console_write(adapter->name);
    console_write(" bound by SeaBIOS ");
    console_write(variant->name);
    console_write(": ");
    console_write(description);
    if (call->framebuffer != 0U) {
        console_write(", linear framebuffer at 0x");
        write_hex(call->framebuffer, 8U);
    }
    console_putc('\n');
    return true;
}

static bool bind_adapter(const struct seavga_variant *variant,
    const struct pci_function *function)
{
    struct seavga_adapter *adapter = free_adapter();
    struct seavga_call call;
    uint32_t command = 0U;

    if (adapter == NULL) {
        report_refusal(variant, "no adapter slot is free");
        return false;
    }
    *adapter = (struct seavga_adapter){ 0 };
    adapter->variant = variant;
    if (hwdrv_pci_claim(function, &adapter->device) != HWDRV_STATUS_OK) {
        report_refusal(variant, "the PCI claim was refused");
        return false;
    }
    adapter->claimed = true;
    adapter->bdf = ((int)function->address.bus << 8) |
        ((int)function->address.device << 3) | (int)function->address.function;
    if (!map_register_bars(adapter) ||
        !hwdrv_pci_config_read(&adapter->device, PCI_REGISTER_COMMAND, 2U,
            &command)) {
        report_refusal(variant, "its BARs could not be mapped");
        (void)hwdrv_pci_release(&adapter->device);
        adapter->claimed = false;
        return false;
    }
    /*
     * The legacy VGA ports have no BAR, so the claim cannot turn their
     * decode on; the firmware that ran the adapter's option ROM did. An
     * adapter it left off is one this driver will not program.
     */
    if (variant->legacy_ports && (command & PCI_COMMAND_IO_SPACE) == 0U) {
        report_refusal(variant, "its legacy VGA I/O decode is off");
        (void)hwdrv_pci_release(&adapter->device);
        adapter->claimed = false;
        return false;
    }
    call = (struct seavga_call){
        .kind = SEAVGA_CALL_BIND,
        .handle = adapter,
        .bdf = adapter->bdf,
        .result = -1
    };
    if (!seabios_host_run(variant->dispatch, &call) || call.result != 0) {
        report_refusal(variant, "the driver's setup failed");
        (void)hwdrv_pci_release(&adapter->device);
        adapter->claimed = false;
        hwdrv_record_probe_failure();
        return false;
    }
    if (call.framebuffer != 0U &&
        !map_framebuffer_bar(adapter, call.framebuffer)) {
        report_refusal(variant, "its framebuffer is in no mappable BAR");
        (void)hwdrv_pci_release(&adapter->device);
        adapter->claimed = false;
        return false;
    }
    return publish_adapter(adapter, &call, function);
}

static bool bind_ramfb(const struct seavga_variant *variant)
{
    struct seavga_adapter *adapter = free_adapter();
    struct seavga_call call;

    if (adapter == NULL) {
        report_refusal(variant, "no adapter slot is free");
        return false;
    }
    *adapter = (struct seavga_adapter){ 0 };
    adapter->variant = variant;
    adapter->bdf = -1;
    call = (struct seavga_call){
        .kind = SEAVGA_CALL_BIND,
        .handle = NULL,
        .bdf = -1,
        .result = -1
    };
    if (!seabios_host_run(variant->dispatch, &call) || call.result != 0) {
        console_write("OpenRFS: SeaBIOS ramfb found no device\n");
        return false;
    }
    return publish_adapter(adapter, &call, NULL);
}

enum hwdrv_status seavga_layer_bind_all(void)
{
    size_t bound = 0U;

    if (hwdrv_get_mode() != HWDRV_MODE_SELECTED) {
        return HWDRV_STATUS_ABSENT;
    }
    for (size_t index = 0U; index < pci_function_count(); ++index) {
        const struct pci_function *function = pci_function_at(index);

        if (function == NULL ||
            function->header_type != PCI_HEADER_TYPE_ENDPOINT ||
            function->class_code != PCI_CLASS_DISPLAY ||
            hwdrv_pci_function_claimed(function)) {
            continue;
        }
        for (size_t slot = 0U; slot < SEAVGA_VARIANT_COUNT; ++slot) {
            const struct seavga_variant *variant = &variants[slot];

            if (variant->match == NULL || !variant->match(function) ||
                !hwdrv_driver_enabled(variant->name)) {
                continue;
            }
            /* One adapter per build, as one VGA BIOS serves one card. */
            if (variant_bound[slot]) {
                report_refusal(variant, "its build already drives an adapter");
                break;
            }
            if (bind_adapter(variant, function)) {
                variant_bound[slot] = true;
                ++bound;
            }
            break;
        }
    }
    for (size_t slot = 0U; slot < SEAVGA_VARIANT_COUNT; ++slot) {
        if (variants[slot].match == NULL && !variant_bound[slot] &&
            hwdrv_driver_enabled(variants[slot].name) &&
            bind_ramfb(&variants[slot])) {
            variant_bound[slot] = true;
            ++bound;
        }
    }
    if (bound != 0U) {
        console_write("OpenRFS: SeaBIOS VGA bound ");
        write_decimal(bound);
        console_write(bound == 1U ? " adapter\n" : " adapters\n");
    }
    return bound != 0U ? HWDRV_STATUS_OK : HWDRV_STATUS_ABSENT;
}
