/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_HWDRV_H
#define OPENRFS_HWDRV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/dma_arena.h>
#include <openrfs/pci.h>
#include <openrfs/pci_resource.h>

/*
 * The upstream driver framework.
 *
 * OpenRFS's own controllers - virtio-net, NVMe, xHCI, HD Audio - are written
 * for this kernel. The drivers bound here are not: they are pinned,
 * byte-for-byte sources from projects that have driven the same hardware on
 * real machines for years (iPXE's network and USB drivers, SeaBIOS's storage,
 * USB and display drivers), compiled against small compatibility layers that
 * give them the environment they were written for.
 *
 * The framework owns what those layers must not decide for themselves: which
 * drivers may bind on this boot, the PCI claim and bus-master transitions a
 * binding needs, and the record of what is bound, from which upstream file,
 * under which licence.
 *
 * Binding is opt-in per boot through the kernel command line:
 *
 *   openrfs.drivers=auto          bind every compiled driver that matches
 *   openrfs.drivers=intel,ahci    bind only the named drivers
 *   openrfs.drivers=none          (the default) bind nothing
 *
 * The default keeps every pre-existing QEMU scenario's device ownership
 * exactly as it was; the interactive boot entry enables auto.
 */
#define HWDRV_MAX_BINDINGS 64U
#define HWDRV_MAX_SELECTED 16U
#define HWDRV_NAME_CAPACITY 24U
#define HWDRV_INSTANCE_CAPACITY 16U
#define HWDRV_DESCRIPTION_CAPACITY 64U

enum hwdrv_status {
    HWDRV_STATUS_OK = 0,
    HWDRV_STATUS_NULL_ARGUMENT,
    HWDRV_STATUS_DISABLED,
    HWDRV_STATUS_ABSENT,
    HWDRV_STATUS_CLAIM_FAILURE,
    HWDRV_STATUS_MAPPING_FAILURE,
    HWDRV_STATUS_IO_FAILURE,
    HWDRV_STATUS_BUS_MASTER_FAILURE,
    HWDRV_STATUS_RELEASE_FAILURE,
    HWDRV_STATUS_NO_MEMORY,
    HWDRV_STATUS_TABLE_FULL,
    HWDRV_STATUS_PROBE_FAILURE,
    HWDRV_STATUS_CONFIGURATION,
    HWDRV_STATUS_COUNT
};

enum hwdrv_mode {
    HWDRV_MODE_NONE = 0,
    HWDRV_MODE_AUTO,
    HWDRV_MODE_SELECTED
};

enum hwdrv_class {
    HWDRV_CLASS_NETWORK = 0,
    HWDRV_CLASS_STORAGE,
    HWDRV_CLASS_USB_HOST,
    HWDRV_CLASS_USB_DEVICE,
    HWDRV_CLASS_DISPLAY,
    HWDRV_CLASS_AUDIO,
    HWDRV_CLASS_INPUT,
    HWDRV_CLASS_SERIAL,
    HWDRV_CLASS_PLATFORM,
    HWDRV_CLASS_COUNT
};

/* Where a driver's code came from, recorded once per compiled driver. */
struct hwdrv_origin {
    const char *project;
    const char *revision;
    const char *path;
    const char *license;
};

/* One bound device, as reported by the drivers command and the tests. */
struct hwdrv_binding {
    char driver[HWDRV_NAME_CAPACITY];
    char instance[HWDRV_INSTANCE_CAPACITY];
    char description[HWDRV_DESCRIPTION_CAPACITY];
    const struct hwdrv_origin *origin;
    enum hwdrv_class device_class;
    struct pci_address address;
    uint16_t vendor_id;
    uint16_t device_id;
    bool pci;
    bool active;
};

/*
 * A claimed PCI function. The claim, its BAR mappings and its bus-master
 * state change only through these helpers, each of which clears IF for the
 * duration of the pci_resource transition it performs and restores it after.
 */
struct hwdrv_pci_device {
    const struct pci_function *function;
    struct pci_device_claim claim;
    struct dma_arena *arena;
    uint8_t mapped_bars[PCI_BAR_COUNT];
    size_t mapped_count;
    bool claimed;
    bool bus_master;
};

struct hwdrv_state {
    enum hwdrv_mode mode;
    size_t selected_count;
    size_t bindings;
    size_t active_bindings;
    size_t claimed_devices;
    size_t bus_masters;
    size_t bind_passes;
    uint64_t probe_failures;
    bool bound;
};

/* Configuration from the kernel command line; safe to call before paging. */
void hwdrv_configure(const char *command_line, size_t length);
enum hwdrv_mode hwdrv_get_mode(void);
bool hwdrv_driver_enabled(const char *name);

/* PCI transitions for compatibility layers. */
enum hwdrv_status hwdrv_pci_claim(
    const struct pci_function *function,
    struct hwdrv_pci_device *device
);
enum hwdrv_status hwdrv_pci_map_bar(
    struct hwdrv_pci_device *device,
    uint8_t bar_index,
    volatile uint8_t **registers,
    uint64_t *size
);
enum hwdrv_status hwdrv_pci_io_bar(
    struct hwdrv_pci_device *device,
    uint8_t bar_index,
    uint16_t *port,
    uint32_t *size
);
enum hwdrv_status hwdrv_pci_enable_bus_master(
    struct hwdrv_pci_device *device,
    struct dma_arena *arena
);
enum hwdrv_status hwdrv_pci_release(struct hwdrv_pci_device *device);
bool hwdrv_pci_config_read(
    const struct hwdrv_pci_device *device,
    uint16_t offset,
    size_t width,
    uint32_t *value
);
bool hwdrv_pci_config_write(
    struct hwdrv_pci_device *device,
    uint16_t offset,
    size_t width,
    uint32_t value
);
/* Configuration read of a function no one has claimed, for matching. */
bool hwdrv_pci_function_read(
    const struct pci_function *function,
    uint16_t offset,
    size_t width,
    uint32_t *value
);
bool hwdrv_pci_function_claimed(const struct pci_function *function);

/* Bounded busy waits on the monotonic clock; usable with IF clear. */
void hwdrv_delay_ns(uint64_t nanoseconds);
void hwdrv_delay_us(uint64_t microseconds);
void hwdrv_delay_ms(uint64_t milliseconds);
uint64_t hwdrv_now_ns(void);

/* Binding records. */
enum hwdrv_status hwdrv_record_binding(
    const char *driver,
    const char *instance,
    const char *description,
    const struct hwdrv_origin *origin,
    enum hwdrv_class device_class,
    const struct pci_function *function
);
void hwdrv_record_probe_failure(void);
size_t hwdrv_binding_count(void);
const struct hwdrv_binding *hwdrv_binding_at(size_t index);
const struct hwdrv_binding *hwdrv_find_binding(const char *instance);
size_t hwdrv_count_class(enum hwdrv_class device_class);

/* Drivers compiled into every layer, and one of them by overall index. */
size_t hwdrv_compiled_driver_count(void);
const char *hwdrv_compiled_driver_name(size_t index);
#define HWDRV_SELF_TEST_CONTROLS 12U

/* Bind every enabled compatibility layer once; the boot stage calls this. */
enum hwdrv_status hwdrv_bind_all(void);
struct hwdrv_state hwdrv_get_state(void);
bool hwdrv_self_test(size_t *completed_tests);
const char *hwdrv_status_string(enum hwdrv_status status);
const char *hwdrv_class_string(enum hwdrv_class device_class);

#endif
