/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DISPLAY_H
#define OPENRFS_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Display adapters driven by upstream drivers.
 *
 * OpenRFS draws on the framebuffer its loader set up. An upstream display
 * driver registers here instead: it can program its adapter into a mode and
 * report the framebuffer that mode scans out. Setting a mode takes the
 * adapter away from whatever was drawing on it, so nothing here does that on
 * its own; only an explicit request does.
 */
#define DISPLAY_MAX_DEVICES 4U
#define DISPLAY_NAME_CAPACITY 12U
#define DISPLAY_DRIVER_CAPACITY 24U
#define DISPLAY_DESCRIPTION_CAPACITY 64U

enum display_status {
    DISPLAY_STATUS_OK = 0,
    DISPLAY_STATUS_NULL_ARGUMENT,
    DISPLAY_STATUS_ABSENT,
    DISPLAY_STATUS_TABLE_FULL,
    DISPLAY_STATUS_NO_SUCH_MODE,
    DISPLAY_STATUS_DEVICE_ERROR,
    DISPLAY_STATUS_BUSY,
    DISPLAY_STATUS_COUNT
};

/* A mode as the adapter scans it out. */
struct display_mode {
    uint32_t width;
    uint32_t height;
    uint32_t bits_per_pixel;
    uint32_t pitch;              /* bytes per scanline */
    uint64_t framebuffer;        /* physical address */
    volatile uint8_t *pixels;    /* where the kernel reaches it */
    uint64_t framebuffer_bytes;  /* pitch * height, all of it mapped */
    uint32_t mode_number;        /* the driver's own mode number */
    bool palette;                /* indexed colour through the DAC */
};

struct display_operations {
    enum display_status (*set_mode)(void *context, uint32_t width,
        uint32_t height, uint32_t bits_per_pixel, struct display_mode *mode);
};

struct display_info {
    char name[DISPLAY_NAME_CAPACITY];
    char driver[DISPLAY_DRIVER_CAPACITY];
    char description[DISPLAY_DESCRIPTION_CAPACITY];
    bool mode_set;
    struct display_mode mode;
};

enum display_status display_register(
    const char *driver,
    const char *description,
    const struct display_operations *operations,
    void *context,
    char name[DISPLAY_NAME_CAPACITY]
);
size_t display_count(void);
bool display_info(size_t index, struct display_info *info);
enum display_status display_find(const char *name, size_t *index);
/*
 * Program a mode and report it. The mode must be exactly the one asked for;
 * a driver never substitutes a nearby one.
 */
enum display_status display_set_mode(size_t index, uint32_t width,
    uint32_t height, uint32_t bits_per_pixel, struct display_mode *mode);
const char *display_status_string(enum display_status status);

#endif
