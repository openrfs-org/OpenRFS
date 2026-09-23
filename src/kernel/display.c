/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The registry of display adapters driven by upstream drivers. See
 * include/openrfs/display.h.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/display.h>

struct display_device {
    char name[DISPLAY_NAME_CAPACITY];
    char driver[DISPLAY_DRIVER_CAPACITY];
    char description[DISPLAY_DESCRIPTION_CAPACITY];
    const struct display_operations *operations;
    void *context;
    struct display_mode mode;
    bool mode_set;
    bool busy;
};

static struct display_device devices[DISPLAY_MAX_DEVICES];
static size_t device_count;

static const char *const status_strings[DISPLAY_STATUS_COUNT] = {
    "ok",
    "null argument",
    "no such display",
    "display table full",
    "the adapter has no such mode",
    "the adapter refused the mode",
    "display busy"
};

static void copy_text(char *destination, const char *source, size_t capacity)
{
    size_t index = 0U;

    while (source != NULL && index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static bool text_equal(const char *left, const char *right)
{
    size_t index = 0U;

    while (left[index] != '\0' && left[index] == right[index]) {
        ++index;
    }
    return left[index] == right[index];
}

static void format_name(char *destination, uint32_t number)
{
    static const char prefix[] = "display";
    char digits[10];
    size_t used = 0U;
    size_t count = 0U;

    while (prefix[used] != '\0' && used + 1U < DISPLAY_NAME_CAPACITY) {
        destination[used] = prefix[used];
        ++used;
    }
    do {
        digits[count++] = (char)('0' + (char)(number % 10U));
        number /= 10U;
    } while (number != 0U && count < sizeof(digits));
    while (count > 0U && used + 1U < DISPLAY_NAME_CAPACITY) {
        destination[used++] = digits[--count];
    }
    destination[used] = '\0';
}

enum display_status display_register(
    const char *driver,
    const char *description,
    const struct display_operations *operations,
    void *context,
    char name[DISPLAY_NAME_CAPACITY]
)
{
    struct display_device *device;

    if (driver == NULL || description == NULL || operations == NULL ||
        operations->set_mode == NULL || name == NULL) {
        return DISPLAY_STATUS_NULL_ARGUMENT;
    }
    if (device_count >= DISPLAY_MAX_DEVICES) {
        return DISPLAY_STATUS_TABLE_FULL;
    }
    device = &devices[device_count];
    format_name(device->name, (uint32_t)device_count);
    copy_text(device->driver, driver, sizeof(device->driver));
    copy_text(device->description, description, sizeof(device->description));
    device->operations = operations;
    device->context = context;
    device->mode = (struct display_mode){ 0 };
    device->mode_set = false;
    device->busy = false;
    ++device_count;
    copy_text(name, device->name, DISPLAY_NAME_CAPACITY);
    return DISPLAY_STATUS_OK;
}

size_t display_count(void)
{
    return device_count;
}

bool display_info(size_t index, struct display_info *info)
{
    const struct display_device *device;

    if (info == NULL || index >= device_count) {
        return false;
    }
    device = &devices[index];
    copy_text(info->name, device->name, sizeof(info->name));
    copy_text(info->driver, device->driver, sizeof(info->driver));
    copy_text(info->description, device->description,
        sizeof(info->description));
    info->mode_set = device->mode_set;
    info->mode = device->mode;
    return true;
}

enum display_status display_find(const char *name, size_t *index)
{
    if (name == NULL || index == NULL) {
        return DISPLAY_STATUS_NULL_ARGUMENT;
    }
    for (size_t slot = 0U; slot < device_count; ++slot) {
        if (text_equal(devices[slot].name, name)) {
            *index = slot;
            return DISPLAY_STATUS_OK;
        }
    }
    return DISPLAY_STATUS_ABSENT;
}

enum display_status display_set_mode(size_t index, uint32_t width,
    uint32_t height, uint32_t bits_per_pixel, struct display_mode *mode)
{
    struct display_device *device;
    struct display_mode programmed = { 0 };
    enum display_status status;

    if (mode == NULL) {
        return DISPLAY_STATUS_NULL_ARGUMENT;
    }
    if (index >= device_count) {
        return DISPLAY_STATUS_ABSENT;
    }
    device = &devices[index];
    if (device->busy) {
        return DISPLAY_STATUS_BUSY;
    }
    device->busy = true;
    status = device->operations->set_mode(device->context, width, height,
        bits_per_pixel, &programmed);
    device->busy = false;
    if (status != DISPLAY_STATUS_OK) {
        return status;
    }
    /* A driver that reports some other mode than the one asked for failed. */
    if (programmed.width != width || programmed.height != height ||
        programmed.bits_per_pixel != bits_per_pixel ||
        programmed.pixels == NULL ||
        programmed.pitch < (uint64_t)width * ((bits_per_pixel + 7U) / 8U) ||
        programmed.framebuffer_bytes <
            (uint64_t)programmed.pitch * height) {
        device->mode_set = false;
        return DISPLAY_STATUS_DEVICE_ERROR;
    }
    device->mode = programmed;
    device->mode_set = true;
    *mode = programmed;
    return DISPLAY_STATUS_OK;
}

const char *display_status_string(enum display_status status)
{
    if ((unsigned int)status >= DISPLAY_STATUS_COUNT) {
        return "unknown display status";
    }
    return status_strings[status];
}
