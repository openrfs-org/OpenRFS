/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_BLOCKDEV_H
#define OPENRFS_BLOCKDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Block devices published by upstream storage drivers.
 *
 * OpenRFS's own filesystems talk to its NVMe driver through volume sessions.
 * The storage drivers bound by the upstream framework - SATA, IDE, SCSI, SD,
 * USB mass storage - publish their media here instead: one registry of
 * devices addressed by logical block, each lending the layer an operations
 * table and a context.
 *
 * The layer owns argument checking, so a driver sees only requests whose
 * range lies inside its medium and whose buffer holds the whole transfer.
 * Every operation is synchronous and bounded by the driver's own device
 * waits. Callers must not hold the buffer in memory a device can reach; the
 * drivers copy through buffers inside their own DMA arenas.
 */
#define BLOCKDEV_MAX_DEVICES 16U
#define BLOCKDEV_NAME_CAPACITY 8U
#define BLOCKDEV_DRIVER_CAPACITY 24U
#define BLOCKDEV_DESCRIPTION_CAPACITY 80U

enum blockdev_status {
    BLOCKDEV_STATUS_OK = 0,
    BLOCKDEV_STATUS_NULL_ARGUMENT,
    BLOCKDEV_STATUS_ABSENT,
    BLOCKDEV_STATUS_TABLE_FULL,
    BLOCKDEV_STATUS_BAD_GEOMETRY,
    BLOCKDEV_STATUS_RANGE,
    BLOCKDEV_STATUS_BUFFER_TOO_SMALL,
    BLOCKDEV_STATUS_READ_ONLY,
    BLOCKDEV_STATUS_UNSUPPORTED,
    BLOCKDEV_STATUS_BUSY,
    BLOCKDEV_STATUS_IO_ERROR,
    BLOCKDEV_STATUS_COUNT
};

enum blockdev_kind {
    BLOCKDEV_KIND_DISK = 0,
    BLOCKDEV_KIND_OPTICAL,
    BLOCKDEV_KIND_FLOPPY,
    BLOCKDEV_KIND_FLASH,
    BLOCKDEV_KIND_COUNT
};

/*
 * Transfers are in whole blocks. flush may be NULL for media without a
 * volatile cache; write may be NULL for read-only media.
 */
struct blockdev_operations {
    enum blockdev_status (*read)(void *context, uint64_t lba, uint32_t count,
        void *buffer);
    enum blockdev_status (*write)(void *context, uint64_t lba, uint32_t count,
        const void *buffer);
    enum blockdev_status (*flush)(void *context);
};

struct blockdev_geometry {
    enum blockdev_kind kind;
    uint32_t block_size;
    uint64_t block_count;
    bool read_only;
    bool removable;
};

struct blockdev_info {
    char name[BLOCKDEV_NAME_CAPACITY];
    char driver[BLOCKDEV_DRIVER_CAPACITY];
    char description[BLOCKDEV_DESCRIPTION_CAPACITY];
    struct blockdev_geometry geometry;
    uint64_t reads;
    uint64_t writes;
    uint64_t flushes;
    uint64_t blocks_read;
    uint64_t blocks_written;
    uint64_t errors;
};

/*
 * Register a medium. Names follow the kind: disk0, cd0, fd0, sd0, counted
 * per kind in registration order. The name is written back when name is not
 * NULL.
 */
enum blockdev_status blockdev_register(
    const char *driver,
    const char *description,
    const struct blockdev_geometry *geometry,
    const struct blockdev_operations *operations,
    void *context,
    char name[BLOCKDEV_NAME_CAPACITY]
);
size_t blockdev_count(void);
bool blockdev_info(size_t index, struct blockdev_info *info);
enum blockdev_status blockdev_find(const char *name, size_t *index);

enum blockdev_status blockdev_read(
    size_t index,
    uint64_t lba,
    uint32_t count,
    void *buffer,
    size_t buffer_size
);
enum blockdev_status blockdev_write(
    size_t index,
    uint64_t lba,
    uint32_t count,
    const void *buffer,
    size_t buffer_size
);
enum blockdev_status blockdev_flush(size_t index);

const char *blockdev_status_string(enum blockdev_status status);
const char *blockdev_kind_string(enum blockdev_kind kind);

/* Argument checks against a synthetic in-memory medium; restores state. */
#define BLOCKDEV_SELF_TEST_CONTROLS 10U
bool blockdev_self_test(size_t *completed);

#endif
