/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The registry of PCM playback devices driven by upstream drivers. See
 * include/openrfs/pcm.h.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/pcm.h>

struct pcm_device {
    char name[PCM_NAME_CAPACITY];
    char driver[PCM_DRIVER_CAPACITY];
    char description[PCM_DESCRIPTION_CAPACITY];
    const struct pcm_operations *operations;
    void *context;
    uint32_t fragment_bytes;
    bool open;
};

static struct pcm_device devices[PCM_MAX_DEVICES];
static size_t device_count;

static const char *const status_strings[PCM_STATUS_COUNT] = {
    "ok",
    "null argument",
    "no such PCM device",
    "PCM device table full",
    "format not supported by the driver",
    "PCM device not open",
    "PCM device busy",
    "timed out waiting for the device",
    "the driver reported an error"
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

enum pcm_status pcm_register(
    const char *driver,
    const char *description,
    const struct pcm_operations *operations,
    void *context,
    char name[PCM_NAME_CAPACITY]
)
{
    struct pcm_device *device;

    if (driver == NULL || description == NULL || operations == NULL ||
        operations->open == NULL || operations->write == NULL ||
        operations->drain == NULL || operations->close == NULL ||
        name == NULL) {
        return PCM_STATUS_NULL_ARGUMENT;
    }
    if (device_count >= PCM_MAX_DEVICES) {
        return PCM_STATUS_TABLE_FULL;
    }
    device = &devices[device_count];
    device->name[0] = 'p';
    device->name[1] = 'c';
    device->name[2] = 'm';
    device->name[3] = (char)('0' + (char)device_count);
    device->name[4] = '\0';
    copy_text(device->driver, driver, sizeof(device->driver));
    copy_text(device->description, description, sizeof(device->description));
    device->operations = operations;
    device->context = context;
    device->fragment_bytes = 0U;
    device->open = false;
    ++device_count;
    copy_text(name, device->name, PCM_NAME_CAPACITY);
    return PCM_STATUS_OK;
}

size_t pcm_count(void)
{
    return device_count;
}

bool pcm_info(size_t index, struct pcm_info *info)
{
    const struct pcm_device *device;

    if (info == NULL || index >= device_count) {
        return false;
    }
    device = &devices[index];
    copy_text(info->name, device->name, sizeof(info->name));
    copy_text(info->driver, device->driver, sizeof(info->driver));
    copy_text(info->description, device->description,
        sizeof(info->description));
    info->open = device->open;
    info->fragment_bytes = device->fragment_bytes;
    return true;
}

enum pcm_status pcm_find_driver(const char *driver, size_t *index)
{
    if (driver == NULL || index == NULL) {
        return PCM_STATUS_NULL_ARGUMENT;
    }
    for (size_t slot = 0U; slot < device_count; ++slot) {
        if (text_equal(devices[slot].driver, driver)) {
            *index = slot;
            return PCM_STATUS_OK;
        }
    }
    return PCM_STATUS_ABSENT;
}

enum pcm_status pcm_open(size_t index, const struct pcm_format *format,
    uint32_t *fragment_bytes)
{
    struct pcm_device *device;
    uint32_t bytes = 0U;
    enum pcm_status status;

    if (format == NULL || fragment_bytes == NULL) {
        return PCM_STATUS_NULL_ARGUMENT;
    }
    if (index >= device_count) {
        return PCM_STATUS_ABSENT;
    }
    device = &devices[index];
    if (device->open) {
        return PCM_STATUS_BUSY;
    }
    status = device->operations->open(device->context, format, &bytes);
    if (status != PCM_STATUS_OK) {
        return status;
    }
    if (bytes == 0U) {
        (void)device->operations->close(device->context);
        return PCM_STATUS_DEVICE_ERROR;
    }
    device->fragment_bytes = bytes;
    device->open = true;
    *fragment_bytes = bytes;
    return PCM_STATUS_OK;
}

enum pcm_status pcm_write(size_t index, const void *fragment,
    size_t length, uint64_t timeout_ns)
{
    struct pcm_device *device;

    if (fragment == NULL) {
        return PCM_STATUS_NULL_ARGUMENT;
    }
    if (index >= device_count) {
        return PCM_STATUS_ABSENT;
    }
    device = &devices[index];
    if (!device->open) {
        return PCM_STATUS_NOT_OPEN;
    }
    /* A driver takes whole fragments only, as MINIX's framework does. */
    if (length != device->fragment_bytes) {
        return PCM_STATUS_UNSUPPORTED;
    }
    return device->operations->write(device->context, fragment, timeout_ns);
}

enum pcm_status pcm_drain(size_t index, uint64_t timeout_ns)
{
    if (index >= device_count) {
        return PCM_STATUS_ABSENT;
    }
    if (!devices[index].open) {
        return PCM_STATUS_NOT_OPEN;
    }
    return devices[index].operations->drain(devices[index].context,
        timeout_ns);
}

enum pcm_status pcm_close(size_t index)
{
    struct pcm_device *device;
    enum pcm_status status;

    if (index >= device_count) {
        return PCM_STATUS_ABSENT;
    }
    device = &devices[index];
    if (!device->open) {
        return PCM_STATUS_NOT_OPEN;
    }
    status = device->operations->close(device->context);
    device->open = false;
    return status;
}

bool pcm_statistics(size_t index, struct pcm_statistics *statistics)
{
    if (statistics == NULL || index >= device_count ||
        devices[index].operations->statistics == NULL) {
        return false;
    }
    devices[index].operations->statistics(devices[index].context,
        statistics);
    return true;
}

const char *pcm_status_string(enum pcm_status status)
{
    if ((unsigned int)status >= PCM_STATUS_COUNT) {
        return "unknown PCM status";
    }
    return status_strings[status];
}
