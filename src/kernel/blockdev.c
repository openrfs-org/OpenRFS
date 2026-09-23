/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The registry of block media published by upstream storage drivers. See
 * include/openrfs/blockdev.h.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/blockdev.h>

struct blockdev_device {
    char name[BLOCKDEV_NAME_CAPACITY];
    char driver[BLOCKDEV_DRIVER_CAPACITY];
    char description[BLOCKDEV_DESCRIPTION_CAPACITY];
    struct blockdev_geometry geometry;
    const struct blockdev_operations *operations;
    void *context;
    uint64_t reads;
    uint64_t writes;
    uint64_t flushes;
    uint64_t blocks_read;
    uint64_t blocks_written;
    uint64_t errors;
    bool busy;
};

static struct blockdev_device devices[BLOCKDEV_MAX_DEVICES];
static size_t device_count;
static uint32_t kind_counts[BLOCKDEV_KIND_COUNT];

static const char *const kind_prefixes[BLOCKDEV_KIND_COUNT] = {
    "disk", "cd", "fd", "sd"
};

static const char *const status_strings[BLOCKDEV_STATUS_COUNT] = {
    "ok",
    "null argument",
    "no such block device",
    "block device table full",
    "unsupported block geometry",
    "block range outside the medium",
    "buffer smaller than the transfer",
    "medium is read-only",
    "operation not supported by the driver",
    "block device busy",
    "device reported an I/O error"
};

static const char *const kind_strings[BLOCKDEV_KIND_COUNT] = {
    "disk", "optical", "floppy", "flash"
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

static bool geometry_valid(const struct blockdev_geometry *geometry)
{
    uint32_t size;

    if (geometry == NULL || geometry->kind >= BLOCKDEV_KIND_COUNT) {
        return false;
    }
    size = geometry->block_size;
    /* 512 bytes to 64 KiB, a power of two, and at least one block. */
    return size >= 512U && size <= 65536U && (size & (size - 1U)) == 0U &&
        geometry->block_count != 0U;
}

static void format_name(char *destination, const char *prefix,
    uint32_t number)
{
    char digits[10];
    size_t used = 0U;
    size_t count = 0U;

    while (prefix[used] != '\0' && used + 1U < BLOCKDEV_NAME_CAPACITY) {
        destination[used] = prefix[used];
        ++used;
    }
    do {
        digits[count++] = (char)('0' + (char)(number % 10U));
        number /= 10U;
    } while (number != 0U && count < sizeof(digits));
    while (count > 0U && used + 1U < BLOCKDEV_NAME_CAPACITY) {
        destination[used++] = digits[--count];
    }
    destination[used] = '\0';
}

/* The checks every transfer passes before a driver sees it. */
static enum blockdev_status check_transfer(
    const struct blockdev_device *device,
    uint64_t lba,
    uint32_t count,
    const void *buffer,
    size_t buffer_size)
{
    uint64_t bytes;

    if (buffer == NULL) {
        return BLOCKDEV_STATUS_NULL_ARGUMENT;
    }
    if (count == 0U || lba >= device->geometry.block_count ||
        count > device->geometry.block_count - lba) {
        return BLOCKDEV_STATUS_RANGE;
    }
    bytes = (uint64_t)count * device->geometry.block_size;
    if (bytes > buffer_size) {
        return BLOCKDEV_STATUS_BUFFER_TOO_SMALL;
    }
    return BLOCKDEV_STATUS_OK;
}

static enum blockdev_status device_read(struct blockdev_device *device,
    uint64_t lba, uint32_t count, void *buffer, size_t buffer_size)
{
    enum blockdev_status status =
        check_transfer(device, lba, count, buffer, buffer_size);

    if (status != BLOCKDEV_STATUS_OK) {
        return status;
    }
    if (device->operations->read == NULL) {
        return BLOCKDEV_STATUS_UNSUPPORTED;
    }
    if (device->busy) {
        return BLOCKDEV_STATUS_BUSY;
    }
    device->busy = true;
    status = device->operations->read(device->context, lba, count, buffer);
    device->busy = false;
    ++device->reads;
    if (status == BLOCKDEV_STATUS_OK) {
        device->blocks_read += count;
    } else {
        ++device->errors;
    }
    return status;
}

static enum blockdev_status device_write(struct blockdev_device *device,
    uint64_t lba, uint32_t count, const void *buffer, size_t buffer_size)
{
    enum blockdev_status status =
        check_transfer(device, lba, count, buffer, buffer_size);

    if (status != BLOCKDEV_STATUS_OK) {
        return status;
    }
    if (device->geometry.read_only) {
        return BLOCKDEV_STATUS_READ_ONLY;
    }
    if (device->operations->write == NULL) {
        return BLOCKDEV_STATUS_UNSUPPORTED;
    }
    if (device->busy) {
        return BLOCKDEV_STATUS_BUSY;
    }
    device->busy = true;
    status = device->operations->write(device->context, lba, count, buffer);
    device->busy = false;
    ++device->writes;
    if (status == BLOCKDEV_STATUS_OK) {
        device->blocks_written += count;
    } else {
        ++device->errors;
    }
    return status;
}

static enum blockdev_status device_flush(struct blockdev_device *device)
{
    enum blockdev_status status;

    if (device->operations->flush == NULL || device->geometry.read_only) {
        return BLOCKDEV_STATUS_OK;
    }
    if (device->busy) {
        return BLOCKDEV_STATUS_BUSY;
    }
    device->busy = true;
    status = device->operations->flush(device->context);
    device->busy = false;
    ++device->flushes;
    if (status != BLOCKDEV_STATUS_OK) {
        ++device->errors;
    }
    return status;
}

enum blockdev_status blockdev_register(
    const char *driver,
    const char *description,
    const struct blockdev_geometry *geometry,
    const struct blockdev_operations *operations,
    void *context,
    char name[BLOCKDEV_NAME_CAPACITY]
)
{
    struct blockdev_device *device;

    if (driver == NULL || description == NULL || geometry == NULL ||
        operations == NULL || operations->read == NULL) {
        return BLOCKDEV_STATUS_NULL_ARGUMENT;
    }
    if (!geometry_valid(geometry)) {
        return BLOCKDEV_STATUS_BAD_GEOMETRY;
    }
    if (device_count >= BLOCKDEV_MAX_DEVICES) {
        return BLOCKDEV_STATUS_TABLE_FULL;
    }
    device = &devices[device_count];
    format_name(device->name, kind_prefixes[geometry->kind],
        kind_counts[geometry->kind]);
    copy_text(device->driver, driver, sizeof(device->driver));
    copy_text(device->description, description, sizeof(device->description));
    device->geometry = *geometry;
    if (operations->write == NULL) {
        device->geometry.read_only = true;
    }
    device->operations = operations;
    device->context = context;
    device->reads = 0U;
    device->writes = 0U;
    device->flushes = 0U;
    device->blocks_read = 0U;
    device->blocks_written = 0U;
    device->errors = 0U;
    device->busy = false;
    ++kind_counts[geometry->kind];
    ++device_count;
    if (name != NULL) {
        copy_text(name, device->name, BLOCKDEV_NAME_CAPACITY);
    }
    return BLOCKDEV_STATUS_OK;
}

size_t blockdev_count(void)
{
    return device_count;
}

bool blockdev_info(size_t index, struct blockdev_info *info)
{
    const struct blockdev_device *device;

    if (info == NULL || index >= device_count) {
        return false;
    }
    device = &devices[index];
    copy_text(info->name, device->name, sizeof(info->name));
    copy_text(info->driver, device->driver, sizeof(info->driver));
    copy_text(info->description, device->description,
        sizeof(info->description));
    info->geometry = device->geometry;
    info->reads = device->reads;
    info->writes = device->writes;
    info->flushes = device->flushes;
    info->blocks_read = device->blocks_read;
    info->blocks_written = device->blocks_written;
    info->errors = device->errors;
    return true;
}

enum blockdev_status blockdev_find(const char *name, size_t *index)
{
    if (name == NULL || index == NULL) {
        return BLOCKDEV_STATUS_NULL_ARGUMENT;
    }
    for (size_t slot = 0U; slot < device_count; ++slot) {
        if (text_equal(devices[slot].name, name)) {
            *index = slot;
            return BLOCKDEV_STATUS_OK;
        }
    }
    return BLOCKDEV_STATUS_ABSENT;
}

enum blockdev_status blockdev_read(
    size_t index,
    uint64_t lba,
    uint32_t count,
    void *buffer,
    size_t buffer_size
)
{
    if (index >= device_count) {
        return BLOCKDEV_STATUS_ABSENT;
    }
    return device_read(&devices[index], lba, count, buffer, buffer_size);
}

enum blockdev_status blockdev_write(
    size_t index,
    uint64_t lba,
    uint32_t count,
    const void *buffer,
    size_t buffer_size
)
{
    if (index >= device_count) {
        return BLOCKDEV_STATUS_ABSENT;
    }
    return device_write(&devices[index], lba, count, buffer, buffer_size);
}

enum blockdev_status blockdev_flush(size_t index)
{
    if (index >= device_count) {
        return BLOCKDEV_STATUS_ABSENT;
    }
    return device_flush(&devices[index]);
}

const char *blockdev_status_string(enum blockdev_status status)
{
    if ((unsigned int)status >= (unsigned int)BLOCKDEV_STATUS_COUNT) {
        return "unknown block device status";
    }
    return status_strings[status];
}

const char *blockdev_kind_string(enum blockdev_kind kind)
{
    if ((unsigned int)kind >= (unsigned int)BLOCKDEV_KIND_COUNT) {
        return "unknown";
    }
    return kind_strings[kind];
}

/* A four-block medium in memory for the self-test. */
#define SELF_TEST_BLOCKS 4U
#define SELF_TEST_BLOCK_SIZE 512U

static uint8_t self_test_medium[SELF_TEST_BLOCKS * SELF_TEST_BLOCK_SIZE];
static uint8_t self_test_buffer[2U * SELF_TEST_BLOCK_SIZE];
static uint32_t self_test_driver_calls;

static enum blockdev_status self_test_read(void *context, uint64_t lba,
    uint32_t count, void *buffer)
{
    uint8_t *destination = buffer;

    (void)context;
    ++self_test_driver_calls;
    for (uint32_t index = 0U; index < count * SELF_TEST_BLOCK_SIZE; ++index) {
        destination[index] =
            self_test_medium[lba * SELF_TEST_BLOCK_SIZE + index];
    }
    return BLOCKDEV_STATUS_OK;
}

static enum blockdev_status self_test_write(void *context, uint64_t lba,
    uint32_t count, const void *buffer)
{
    const uint8_t *source = buffer;

    (void)context;
    ++self_test_driver_calls;
    for (uint32_t index = 0U; index < count * SELF_TEST_BLOCK_SIZE; ++index) {
        self_test_medium[lba * SELF_TEST_BLOCK_SIZE + index] = source[index];
    }
    return BLOCKDEV_STATUS_OK;
}

static const struct blockdev_operations self_test_operations = {
    .read = self_test_read,
    .write = self_test_write,
    .flush = NULL
};

bool blockdev_self_test(size_t *completed)
{
    struct blockdev_device device;
    struct blockdev_geometry bad = {
        .kind = BLOCKDEV_KIND_DISK,
        .block_size = 768U,
        .block_count = 1U,
        .read_only = false,
        .removable = false
    };
    size_t passed = 0U;
    bool ok = true;

    if (completed == NULL) {
        return false;
    }
    for (size_t index = 0U; index < sizeof(device); ++index) {
        ((uint8_t *)&device)[index] = 0U;
    }
    for (size_t index = 0U; index < sizeof(self_test_medium); ++index) {
        self_test_medium[index] = (uint8_t)(index * 7U + 3U);
    }
    device.geometry.kind = BLOCKDEV_KIND_DISK;
    device.geometry.block_size = SELF_TEST_BLOCK_SIZE;
    device.geometry.block_count = SELF_TEST_BLOCKS;
    device.operations = &self_test_operations;
    self_test_driver_calls = 0U;

    /* 1: an in-range read returns the medium's bytes. */
    if (ok && device_read(&device, 1U, 2U, self_test_buffer,
            sizeof(self_test_buffer)) == BLOCKDEV_STATUS_OK &&
        self_test_buffer[0] == self_test_medium[SELF_TEST_BLOCK_SIZE] &&
        self_test_buffer[1023] ==
            self_test_medium[SELF_TEST_BLOCK_SIZE + 1023U]) {
        ++passed;
    } else {
        ok = false;
    }
    /* 2: an in-range write reaches the medium. */
    for (size_t index = 0U; index < SELF_TEST_BLOCK_SIZE; ++index) {
        self_test_buffer[index] = (uint8_t)(0xA5U ^ index);
    }
    if (ok && device_write(&device, 3U, 1U, self_test_buffer,
            SELF_TEST_BLOCK_SIZE) == BLOCKDEV_STATUS_OK &&
        self_test_medium[3U * SELF_TEST_BLOCK_SIZE + 17U] ==
            (uint8_t)(0xA5U ^ 17U)) {
        ++passed;
    } else {
        ok = false;
    }
    /* 3: a start block past the end is refused. */
    if (ok && device_read(&device, SELF_TEST_BLOCKS, 1U, self_test_buffer,
            sizeof(self_test_buffer)) == BLOCKDEV_STATUS_RANGE) {
        ++passed;
    } else {
        ok = false;
    }
    /* 4: a run crossing the end is refused, including by overflow. */
    if (ok && device_read(&device, 3U, 2U, self_test_buffer,
            sizeof(self_test_buffer)) == BLOCKDEV_STATUS_RANGE &&
        device_read(&device, UINT64_MAX, 2U, self_test_buffer,
            sizeof(self_test_buffer)) == BLOCKDEV_STATUS_RANGE) {
        ++passed;
    } else {
        ok = false;
    }
    /* 5: a zero-block transfer is refused. */
    if (ok && device_read(&device, 0U, 0U, self_test_buffer,
            sizeof(self_test_buffer)) == BLOCKDEV_STATUS_RANGE) {
        ++passed;
    } else {
        ok = false;
    }
    /* 6: a buffer smaller than the transfer is refused. */
    if (ok && device_read(&device, 0U, 2U, self_test_buffer,
            SELF_TEST_BLOCK_SIZE) == BLOCKDEV_STATUS_BUFFER_TOO_SMALL) {
        ++passed;
    } else {
        ok = false;
    }
    /* 7: a missing buffer is refused. */
    if (ok && device_read(&device, 0U, 1U, NULL, SELF_TEST_BLOCK_SIZE) ==
            BLOCKDEV_STATUS_NULL_ARGUMENT) {
        ++passed;
    } else {
        ok = false;
    }
    /* 8: a read-only medium refuses writes before the driver sees them. */
    device.geometry.read_only = true;
    if (ok && device_write(&device, 0U, 1U, self_test_buffer,
            SELF_TEST_BLOCK_SIZE) == BLOCKDEV_STATUS_READ_ONLY) {
        ++passed;
    } else {
        ok = false;
    }
    device.geometry.read_only = false;
    /* 9: flushing a medium without a cache succeeds without a driver call. */
    if (ok && device_flush(&device) == BLOCKDEV_STATUS_OK) {
        ++passed;
    } else {
        ok = false;
    }
    /* 10: unsupported geometry is refused; the driver saw exactly two calls. */
    if (ok && !geometry_valid(&bad) && !geometry_valid(NULL) &&
        self_test_driver_calls == 2U && device.errors == 0U &&
        device.blocks_read == 2U && device.blocks_written == 1U) {
        ++passed;
    } else {
        ok = false;
    }
    *completed = passed;
    return ok && passed == BLOCKDEV_SELF_TEST_CONTROLS;
}
