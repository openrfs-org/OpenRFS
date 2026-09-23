/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The upstream driver framework: boot-time configuration, the PCI transitions
 * compatibility layers are allowed to request, and the binding record. See
 * include/openrfs/hwdrv.h.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/clock.h>
#include <openrfs/console.h>
#include <openrfs/cpu.h>
#include <openrfs/dma_arena.h>
#include <openrfs/hwdrv.h>
#include <openrfs/hwdrv_layers.h>
#include <openrfs/pci.h>
#include <openrfs/pci_resource.h>

#define HWDRV_OPTION_PREFIX "openrfs.drivers="
#define PCI_REGISTER_BAR_FIRST UINT16_C(0x10)
#define PCI_REGISTER_BAR_LIMIT UINT16_C(0x28)
#define PCI_REGISTER_EXPANSION_ROM UINT16_C(0x30)
#define PCI_COMMAND_AUTHORITY_MASK \
    (PCI_COMMAND_IO_SPACE | PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)

struct hwdrv_configuration {
    enum hwdrv_mode mode;
    size_t selected_count;
    char selected[HWDRV_MAX_SELECTED][HWDRV_NAME_CAPACITY];
    bool malformed;
};

static struct hwdrv_configuration configuration;
static struct hwdrv_binding bindings[HWDRV_MAX_BINDINGS];
static size_t binding_count;
static size_t claimed_devices;
static size_t bus_masters;
static size_t bind_passes;
static uint64_t probe_failures;
static bool bound;

static void zero_bytes(void *pointer, size_t length)
{
    uint8_t *bytes = pointer;

    for (size_t index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

static size_t bounded_length(const char *text, size_t capacity)
{
    size_t length = 0U;

    while (text != NULL && length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool text_equal(const char *left, const char *right, size_t capacity)
{
    for (size_t index = 0U; index < capacity; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
        if (left[index] == '\0') {
            return true;
        }
    }
    return true;
}

static void copy_text(char *destination, const char *source, size_t capacity)
{
    size_t index = 0U;

    if (capacity == 0U) {
        return;
    }
    while (source != NULL && index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static bool token_is(const char *token, size_t length, const char *word)
{
    size_t index = 0U;

    while (index < length && word[index] != '\0' &&
        token[index] == word[index]) {
        ++index;
    }
    return index == length && word[index] == '\0';
}

static bool irq_save(void)
{
    const bool enabled = cpu_interrupts_enabled();

    cpu_interrupt_disable();
    return enabled;
}

static void irq_restore(bool enabled)
{
    if (enabled) {
        cpu_interrupt_enable();
    }
}

/*
 * Parse one openrfs.drivers= value into a configuration. The value is a
 * comma-separated list of lower-case driver names, or one of the two words.
 * Anything else - an empty list, an over-long name, too many names, a
 * character outside [a-z0-9_-] - leaves binding disabled and marks the
 * configuration malformed, so a typo cannot silently bind more than asked.
 */
static void parse_value(
    const char *value,
    size_t length,
    struct hwdrv_configuration *result
)
{
    size_t start = 0U;

    zero_bytes(result, sizeof(*result));
    if (token_is(value, length, "auto")) {
        result->mode = HWDRV_MODE_AUTO;
        return;
    }
    if (length == 0U || token_is(value, length, "none")) {
        result->mode = HWDRV_MODE_NONE;
        return;
    }
    while (start <= length) {
        size_t end = start;

        while (end < length && value[end] != ',') {
            const char character = value[end];

            if (!((character >= 'a' && character <= 'z') ||
                    (character >= '0' && character <= '9') ||
                    character == '_' || character == '-')) {
                zero_bytes(result, sizeof(*result));
                result->malformed = true;
                return;
            }
            ++end;
        }
        if (end == start || end - start >= HWDRV_NAME_CAPACITY ||
            result->selected_count >= HWDRV_MAX_SELECTED) {
            zero_bytes(result, sizeof(*result));
            result->malformed = true;
            return;
        }
        for (size_t index = 0U; index < end - start; ++index) {
            result->selected[result->selected_count][index] =
                value[start + index];
        }
        result->selected[result->selected_count][end - start] = '\0';
        ++result->selected_count;
        start = end + 1U;
    }
    result->mode = HWDRV_MODE_SELECTED;
}

static void parse_command_line(
    const char *command_line,
    size_t length,
    struct hwdrv_configuration *result
)
{
    const size_t prefix_length = bounded_length(HWDRV_OPTION_PREFIX, 32U);
    size_t offset = 0U;
    bool seen = false;

    zero_bytes(result, sizeof(*result));
    while (command_line != NULL && offset < length) {
        size_t token_start;
        size_t token_length;
        bool prefixed = true;

        while (offset < length && command_line[offset] == ' ') {
            ++offset;
        }
        token_start = offset;
        while (offset < length && command_line[offset] != ' ' &&
            command_line[offset] != '\0') {
            ++offset;
        }
        token_length = offset - token_start;
        if (token_length < prefix_length) {
            continue;
        }
        for (size_t index = 0U; index < prefix_length; ++index) {
            if (command_line[token_start + index] !=
                HWDRV_OPTION_PREFIX[index]) {
                prefixed = false;
                break;
            }
        }
        if (!prefixed) {
            continue;
        }
        if (seen) {
            /* Two answers to one question is no answer. */
            zero_bytes(result, sizeof(*result));
            result->malformed = true;
            return;
        }
        seen = true;
        parse_value(command_line + token_start + prefix_length,
            token_length - prefix_length, result);
        if (result->malformed) {
            return;
        }
    }
}

void hwdrv_configure(const char *command_line, size_t length)
{
    parse_command_line(command_line, length, &configuration);
}

enum hwdrv_mode hwdrv_get_mode(void)
{
    return configuration.mode;
}

static bool configuration_enables(
    const struct hwdrv_configuration *candidate,
    const char *name
)
{
    if (name == NULL) {
        return false;
    }
    if (candidate->mode == HWDRV_MODE_AUTO) {
        return true;
    }
    if (candidate->mode != HWDRV_MODE_SELECTED) {
        return false;
    }
    for (size_t index = 0U; index < candidate->selected_count; ++index) {
        if (text_equal(candidate->selected[index], name,
                HWDRV_NAME_CAPACITY)) {
            return true;
        }
    }
    return false;
}

bool hwdrv_driver_enabled(const char *name)
{
    return configuration_enables(&configuration, name);
}

enum hwdrv_status hwdrv_pci_claim(
    const struct pci_function *function,
    struct hwdrv_pci_device *device
)
{
    enum pci_resource_status status;
    bool enabled;

    if (function == NULL || device == NULL) {
        return HWDRV_STATUS_NULL_ARGUMENT;
    }
    zero_bytes(device, sizeof(*device));
    enabled = irq_save();
    status = pci_claim_device(function, &device->claim);
    irq_restore(enabled);
    if (status != PCI_RESOURCE_STATUS_OK) {
        return HWDRV_STATUS_CLAIM_FAILURE;
    }
    device->function = function;
    device->claimed = true;
    ++claimed_devices;
    return HWDRV_STATUS_OK;
}

enum hwdrv_status hwdrv_pci_map_bar(
    struct hwdrv_pci_device *device,
    uint8_t bar_index,
    volatile uint8_t **registers,
    uint64_t *size
)
{
    struct pci_mmio_region *region;
    volatile void *pointer = NULL;
    enum pci_resource_status status;
    bool enabled;

    if (device == NULL || registers == NULL || !device->claimed) {
        return HWDRV_STATUS_NULL_ARGUMENT;
    }
    region = pci_claim_mapped_bar(&device->claim, bar_index);
    if (region == NULL) {
        enabled = irq_save();
        status = pci_claim_map_bar(&device->claim, bar_index, &region);
        irq_restore(enabled);
        if (status != PCI_RESOURCE_STATUS_OK || region == NULL) {
            return HWDRV_STATUS_MAPPING_FAILURE;
        }
        if (device->mapped_count < PCI_BAR_COUNT) {
            device->mapped_bars[device->mapped_count++] = bar_index;
        }
    }
    if (pci_mmio_subregion(region, 0U, region->size, &pointer) !=
            PCI_RESOURCE_STATUS_OK || pointer == NULL) {
        return HWDRV_STATUS_MAPPING_FAILURE;
    }
    *registers = (volatile uint8_t *)pointer;
    if (size != NULL) {
        *size = region->size;
    }
    return HWDRV_STATUS_OK;
}

enum hwdrv_status hwdrv_pci_io_bar(
    struct hwdrv_pci_device *device,
    uint8_t bar_index,
    uint16_t *port,
    uint32_t *size
)
{
    const struct pci_bar_description *bar;
    enum pci_resource_status status;
    bool enabled;

    if (device == NULL || port == NULL || !device->claimed) {
        return HWDRV_STATUS_NULL_ARGUMENT;
    }
    bar = pci_claim_bar(&device->claim, bar_index);
    if (bar == NULL || !bar->implemented || bar->kind != PCI_BAR_IO ||
        bar->base > UINT16_MAX || bar->size > UINT32_C(0x10000) ||
        bar->base + bar->size - 1U > UINT16_MAX) {
        return HWDRV_STATUS_IO_FAILURE;
    }
    enabled = irq_save();
    status = pci_claim_enable_io(&device->claim);
    irq_restore(enabled);
    if (status != PCI_RESOURCE_STATUS_OK) {
        return HWDRV_STATUS_IO_FAILURE;
    }
    *port = (uint16_t)bar->base;
    if (size != NULL) {
        *size = (uint32_t)bar->size;
    }
    return HWDRV_STATUS_OK;
}

enum hwdrv_status hwdrv_pci_enable_bus_master(
    struct hwdrv_pci_device *device,
    struct dma_arena *arena
)
{
    struct pci_bus_master_request request;
    enum pci_resource_status status;
    bool enabled;

    if (device == NULL || arena == NULL || !device->claimed) {
        return HWDRV_STATUS_NULL_ARGUMENT;
    }
    if (device->bus_master) {
        return HWDRV_STATUS_OK;
    }
    zero_bytes(&request, sizeof(request));
    request.allocations[0] = &arena->allocation;
    request.allocation_count = 1U;
    enabled = irq_save();
    status = pci_claim_enable_bus_master(&device->claim, &request);
    irq_restore(enabled);
    if (status != PCI_RESOURCE_STATUS_OK) {
        return HWDRV_STATUS_BUS_MASTER_FAILURE;
    }
    device->arena = arena;
    device->bus_master = true;
    ++bus_masters;
    return HWDRV_STATUS_OK;
}

enum hwdrv_status hwdrv_pci_release(struct hwdrv_pci_device *device)
{
    enum pci_resource_status status;
    bool enabled;

    if (device == NULL) {
        return HWDRV_STATUS_NULL_ARGUMENT;
    }
    if (!device->claimed) {
        return HWDRV_STATUS_OK;
    }
    enabled = irq_save();
    /* Bus mastering is disabled inside release before any mapping goes. */
    status = pci_release_device(&device->claim);
    irq_restore(enabled);
    if (status != PCI_RESOURCE_STATUS_OK) {
        return HWDRV_STATUS_RELEASE_FAILURE;
    }
    if (device->bus_master && bus_masters > 0U) {
        --bus_masters;
    }
    if (claimed_devices > 0U) {
        --claimed_devices;
    }
    device->bus_master = false;
    device->claimed = false;
    device->mapped_count = 0U;
    return HWDRV_STATUS_OK;
}

static bool config_read_address(
    struct pci_address address,
    uint16_t offset,
    size_t width,
    uint32_t *value
)
{
    uint32_t dword = 0U;
    const unsigned int shift = (unsigned int)(offset & 3U) * 8U;

    if (value == NULL || offset >= PCI_CONFIG_SPACE_SIZE ||
        (width != 1U && width != 2U && width != 4U) ||
        (offset & (uint16_t)(width - 1U)) != 0U) {
        return false;
    }
    if (pci_config_read_port(address, (uint16_t)(offset & ~UINT16_C(3)),
            &dword) != PCI_STATUS_OK) {
        return false;
    }
    if (width == 4U) {
        *value = dword;
    } else if (width == 2U) {
        *value = (dword >> shift) & UINT32_C(0xFFFF);
    } else {
        *value = (dword >> shift) & UINT32_C(0xFF);
    }
    return true;
}

bool hwdrv_pci_config_read(
    const struct hwdrv_pci_device *device,
    uint16_t offset,
    size_t width,
    uint32_t *value
)
{
    if (device == NULL || !device->claimed) {
        return false;
    }
    return config_read_address(device->claim.device, offset, width, value);
}

bool hwdrv_pci_function_read(
    const struct pci_function *function,
    uint16_t offset,
    size_t width,
    uint32_t *value
)
{
    if (function == NULL) {
        return false;
    }
    return config_read_address(function->address, offset, width, value);
}

/*
 * A driver asking for a command-register change is asking for authority.
 * Decode and bus-master bits move only through the claim - I/O decode through
 * pci_claim_enable_io, bus mastering through the arena the device was given -
 * and the harmless bits through pci_claim_update_command. BARs and the
 * expansion ROM belong to the claim and are never rewritten by a driver.
 */
static bool write_command_register(
    struct hwdrv_pci_device *device,
    uint16_t requested
)
{
    const uint16_t current = device->claim.current_command;
    const uint16_t authority_wanted = requested & PCI_COMMAND_AUTHORITY_MASK;
    const uint16_t authority_held = current & PCI_COMMAND_AUTHORITY_MASK;
    uint16_t unprivileged = requested & (uint16_t)~PCI_COMMAND_AUTHORITY_MASK;
    bool enabled;
    enum pci_resource_status status = PCI_RESOURCE_STATUS_OK;

    if ((unprivileged & (uint16_t)~PCI_COMMAND_UNPRIVILEGED_MASK) != 0U) {
        /* Bits the kernel does not grant are dropped, not written. */
        unprivileged &= PCI_COMMAND_UNPRIVILEGED_MASK;
    }
    if ((authority_wanted & PCI_COMMAND_IO_SPACE) != 0U &&
        (authority_held & PCI_COMMAND_IO_SPACE) == 0U) {
        enabled = irq_save();
        status = pci_claim_enable_io(&device->claim);
        irq_restore(enabled);
        if (status != PCI_RESOURCE_STATUS_OK) {
            return false;
        }
    }
    if ((authority_wanted & PCI_COMMAND_MEMORY_SPACE) != 0U &&
        (authority_held & PCI_COMMAND_MEMORY_SPACE) == 0U) {
        /* Memory decode follows from mapping a BAR, not from a request. */
        return false;
    }
    if ((authority_wanted & PCI_COMMAND_BUS_MASTER) != 0U &&
        !device->bus_master) {
        if (device->arena == NULL ||
            hwdrv_pci_enable_bus_master(device, device->arena) !=
                HWDRV_STATUS_OK) {
            return false;
        }
    }
    if ((authority_wanted & PCI_COMMAND_BUS_MASTER) == 0U &&
        device->bus_master) {
        enabled = irq_save();
        status = pci_claim_disable_bus_master(&device->claim);
        irq_restore(enabled);
        if (status != PCI_RESOURCE_STATUS_OK) {
            return false;
        }
        device->bus_master = false;
        if (bus_masters > 0U) {
            --bus_masters;
        }
    }
    enabled = irq_save();
    status = pci_claim_update_command(&device->claim,
        PCI_COMMAND_UNPRIVILEGED_MASK, unprivileged);
    irq_restore(enabled);
    return status == PCI_RESOURCE_STATUS_OK;
}

bool hwdrv_pci_config_write(
    struct hwdrv_pci_device *device,
    uint16_t offset,
    size_t width,
    uint32_t value
)
{
    if (device == NULL || !device->claimed ||
        offset >= PCI_CONFIG_SPACE_SIZE ||
        (width != 1U && width != 2U && width != 4U) ||
        (offset & (uint16_t)(width - 1U)) != 0U) {
        return false;
    }
    if (offset < PCI_REGISTER_STATUS &&
        offset + width > PCI_REGISTER_COMMAND) {
        if (offset != PCI_REGISTER_COMMAND || width == 1U) {
            return false;
        }
        if (!write_command_register(device, (uint16_t)value)) {
            return false;
        }
        if (width == 4U) {
            /* The status half is write-one-to-clear; pass it through. */
            return pci_config_write_port(device->claim.device,
                PCI_REGISTER_STATUS, sizeof(uint16_t),
                (value >> 16U) & UINT32_C(0xFFFF)) == PCI_STATUS_OK;
        }
        return true;
    }
    if ((offset + width > PCI_REGISTER_BAR_FIRST &&
            offset < PCI_REGISTER_BAR_LIMIT) ||
        (offset + width > PCI_REGISTER_EXPANSION_ROM &&
            offset < PCI_REGISTER_EXPANSION_ROM + 4U)) {
        return false;
    }
    return pci_config_write_port(device->claim.device, offset, width,
        value) == PCI_STATUS_OK;
}

bool hwdrv_pci_function_claimed(const struct pci_function *function)
{
    if (function == NULL) {
        return false;
    }
    for (size_t index = 0U; index < binding_count; ++index) {
        const struct hwdrv_binding *binding = &bindings[index];

        if (binding->active && binding->pci &&
            binding->address.segment == function->address.segment &&
            binding->address.bus == function->address.bus &&
            binding->address.device == function->address.device &&
            binding->address.function == function->address.function) {
            return true;
        }
    }
    return false;
}

uint64_t hwdrv_now_ns(void)
{
    return clock_monotonic_ns();
}

void hwdrv_delay_ns(uint64_t nanoseconds)
{
    const uint64_t start = clock_monotonic_ns();
    const uint64_t deadline = nanoseconds > UINT64_MAX - start ?
        UINT64_MAX : start + nanoseconds;

    while (clock_monotonic_ns() < deadline) {
        __asm__ volatile ("pause" : : : "memory");
    }
}

void hwdrv_delay_us(uint64_t microseconds)
{
    hwdrv_delay_ns(microseconds > UINT64_MAX / UINT64_C(1000) ?
        UINT64_MAX : microseconds * UINT64_C(1000));
}

void hwdrv_delay_ms(uint64_t milliseconds)
{
    hwdrv_delay_ns(milliseconds > UINT64_MAX / UINT64_C(1000000) ?
        UINT64_MAX : milliseconds * UINT64_C(1000000));
}

enum hwdrv_status hwdrv_record_binding(
    const char *driver,
    const char *instance,
    const char *description,
    const struct hwdrv_origin *origin,
    enum hwdrv_class device_class,
    const struct pci_function *function
)
{
    struct hwdrv_binding *binding;

    if (driver == NULL || instance == NULL || origin == NULL ||
        device_class >= HWDRV_CLASS_COUNT) {
        return HWDRV_STATUS_NULL_ARGUMENT;
    }
    if (binding_count >= HWDRV_MAX_BINDINGS) {
        return HWDRV_STATUS_TABLE_FULL;
    }
    binding = &bindings[binding_count];
    zero_bytes(binding, sizeof(*binding));
    copy_text(binding->driver, driver, sizeof(binding->driver));
    copy_text(binding->instance, instance, sizeof(binding->instance));
    copy_text(binding->description, description,
        sizeof(binding->description));
    binding->origin = origin;
    binding->device_class = device_class;
    if (function != NULL) {
        binding->pci = true;
        binding->address = function->address;
        binding->vendor_id = function->vendor_id;
        binding->device_id = function->device_id;
    }
    binding->active = true;
    ++binding_count;
    return HWDRV_STATUS_OK;
}

void hwdrv_record_probe_failure(void)
{
    ++probe_failures;
}

size_t hwdrv_binding_count(void)
{
    return binding_count;
}

const struct hwdrv_binding *hwdrv_binding_at(size_t index)
{
    return index < binding_count ? &bindings[index] : NULL;
}

const struct hwdrv_binding *hwdrv_find_binding(const char *instance)
{
    if (instance == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < binding_count; ++index) {
        if (bindings[index].active &&
            text_equal(bindings[index].instance, instance,
                HWDRV_INSTANCE_CAPACITY)) {
            return &bindings[index];
        }
    }
    return NULL;
}

size_t hwdrv_count_class(enum hwdrv_class device_class)
{
    size_t count = 0U;

    for (size_t index = 0U; index < binding_count; ++index) {
        if (bindings[index].active &&
            bindings[index].device_class == device_class) {
            ++count;
        }
    }
    return count;
}

size_t hwdrv_compiled_driver_count(void)
{
    size_t total = 0U;

    for (size_t index = 0U; index < hwdrv_layer_count(); ++index) {
        const struct hwdrv_layer *layer = hwdrv_layer_at(index);

        if (layer != NULL && layer->driver_count != NULL) {
            total += layer->driver_count();
        }
    }
    return total;
}

const char *hwdrv_compiled_driver_name(size_t index)
{
    for (size_t layer_index = 0U; layer_index < hwdrv_layer_count();
         ++layer_index) {
        const struct hwdrv_layer *layer = hwdrv_layer_at(layer_index);
        size_t count;

        if (layer == NULL || layer->driver_count == NULL ||
            layer->driver_name == NULL) {
            continue;
        }
        count = layer->driver_count();
        if (index < count) {
            return layer->driver_name(index);
        }
        index -= count;
    }
    return NULL;
}

enum hwdrv_status hwdrv_bind_all(void)
{
    size_t before;

    if (configuration.mode == HWDRV_MODE_NONE) {
        return HWDRV_STATUS_DISABLED;
    }
    if (bound) {
        return HWDRV_STATUS_OK;
    }
    if (!pci_is_initialized() || !pci_resource_get_state().active) {
        return HWDRV_STATUS_CONFIGURATION;
    }
    before = binding_count;
    ++bind_passes;
    for (size_t index = 0U; index < hwdrv_layer_count(); ++index) {
        const struct hwdrv_layer *layer = hwdrv_layer_at(index);

        if (layer == NULL || layer->bind_all == NULL) {
            continue;
        }
        (void)layer->bind_all();
    }
    bound = true;
    return binding_count > before ? HWDRV_STATUS_OK : HWDRV_STATUS_ABSENT;
}

struct hwdrv_state hwdrv_get_state(void)
{
    struct hwdrv_state state;

    zero_bytes(&state, sizeof(state));
    state.mode = configuration.mode;
    state.selected_count = configuration.selected_count;
    state.bindings = binding_count;
    state.active_bindings = 0U;
    for (size_t index = 0U; index < binding_count; ++index) {
        if (bindings[index].active) {
            ++state.active_bindings;
        }
    }
    state.claimed_devices = claimed_devices;
    state.bus_masters = bus_masters;
    state.bind_passes = bind_passes;
    state.probe_failures = probe_failures;
    state.bound = bound;
    return state;
}

/*
 * Controls over the configuration grammar. They parse synthetic command
 * lines into a scratch configuration, so the installed one - which decides
 * what binds on this boot - is never disturbed by its own proof.
 */
bool hwdrv_self_test(size_t *completed_tests)
{
    static const char auto_line[] = "openrfs.test=x openrfs.drivers=auto";
    static const char list_line[] = "openrfs.drivers=intel,ahci,usb-hid";
    static const char bad_line[] = "openrfs.drivers=Intel";
    static const char twice_line[] =
        "openrfs.drivers=auto openrfs.drivers=none";
    static const char empty_item_line[] = "openrfs.drivers=intel,,ahci";
    static const char absent_line[] = "openrfs.test=normal";
    struct hwdrv_configuration scratch;
    size_t arena_tests = 0U;
    size_t completed = 0U;

    if (completed_tests != NULL) {
        *completed_tests = 0U;
    }
    parse_command_line(auto_line, sizeof(auto_line) - 1U, &scratch);
    if (scratch.mode != HWDRV_MODE_AUTO || scratch.malformed ||
        !configuration_enables(&scratch, "anything")) {
        return false;
    }
    ++completed;
    parse_command_line(list_line, sizeof(list_line) - 1U, &scratch);
    if (scratch.mode != HWDRV_MODE_SELECTED || scratch.selected_count != 3U ||
        !configuration_enables(&scratch, "usb-hid") ||
        configuration_enables(&scratch, "realtek")) {
        return false;
    }
    ++completed;
    parse_command_line(bad_line, sizeof(bad_line) - 1U, &scratch);
    if (scratch.mode != HWDRV_MODE_NONE || !scratch.malformed ||
        configuration_enables(&scratch, "intel")) {
        return false;
    }
    ++completed;
    parse_command_line(twice_line, sizeof(twice_line) - 1U, &scratch);
    if (scratch.mode != HWDRV_MODE_NONE || !scratch.malformed) {
        return false;
    }
    ++completed;
    parse_command_line(empty_item_line, sizeof(empty_item_line) - 1U,
        &scratch);
    if (scratch.mode != HWDRV_MODE_NONE || !scratch.malformed) {
        return false;
    }
    ++completed;
    parse_command_line(absent_line, sizeof(absent_line) - 1U, &scratch);
    if (scratch.mode != HWDRV_MODE_NONE || scratch.malformed ||
        configuration_enables(&scratch, "intel")) {
        return false;
    }
    ++completed;
    if (!dma_arena_self_test(&arena_tests) || arena_tests != 6U) {
        return false;
    }
    completed += arena_tests;
    if (completed_tests != NULL) {
        *completed_tests = completed;
    }
    return true;
}

const char *hwdrv_status_string(enum hwdrv_status status)
{
    static const char *const messages[HWDRV_STATUS_COUNT] = {
        "ok", "null driver framework argument",
        "upstream drivers are disabled on this boot",
        "no matching device is present", "PCI claim failed",
        "BAR mapping failed", "I/O BAR could not be enabled",
        "bus mastering could not be enabled", "PCI release failed",
        "driver memory is exhausted", "binding table is full",
        "driver probe failed", "driver configuration is invalid"
    };

    _Static_assert(sizeof(messages) / sizeof(messages[0]) ==
        HWDRV_STATUS_COUNT, "driver framework messages are out of sync");
    if (status < HWDRV_STATUS_OK || status >= HWDRV_STATUS_COUNT) {
        return "unknown driver framework status";
    }
    return messages[status];
}

const char *hwdrv_class_string(enum hwdrv_class device_class)
{
    static const char *const names[HWDRV_CLASS_COUNT] = {
        "network", "storage", "usb-host", "usb-device", "display", "audio",
        "input", "serial", "platform"
    };

    if (device_class >= HWDRV_CLASS_COUNT) {
        return "unknown";
    }
    return names[device_class];
}
