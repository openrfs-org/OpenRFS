/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_FAT32_BACKEND_H
#define OPENRFS_FAT32_BACKEND_H

#include <openrfs/fat32_fs.h>

/* Private VFS backend contract; kernel services use the openrfsfs_* VFS API. */
bool fat32_backend_self_test(size_t *completed_tests);
void fat32_backend_initialize(void);
enum openrfsfs_status fat32_backend_mount(enum openrfsfs_volume volume);
enum openrfsfs_status fat32_backend_unmount(enum openrfsfs_volume volume);
enum openrfsfs_status fat32_backend_sync(enum openrfsfs_volume volume);
struct openrfsfs_drive_info fat32_backend_drive(enum openrfsfs_volume volume);
uint64_t fat32_backend_completion_count(enum openrfsfs_volume volume);
enum openrfsfs_status fat32_backend_open(
    enum openrfsfs_volume volume,
    const char *path,
    enum openrfsfs_access access,
    openrfsfs_handle *handle
);
enum openrfsfs_status fat32_backend_close(openrfsfs_handle handle);
enum openrfsfs_status fat32_backend_read(
    openrfsfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    size_t *read_bytes
);
enum openrfsfs_status fat32_backend_pread(
    openrfsfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    uint64_t offset,
    size_t *read_bytes
);
enum openrfsfs_status fat32_backend_write(
    openrfsfs_handle handle,
    const uint8_t *source,
    size_t source_bytes,
    size_t *written_bytes
);
enum openrfsfs_status fat32_backend_seek(
    openrfsfs_handle handle,
    int64_t offset,
    enum openrfsfs_seek_origin origin,
    uint64_t *position
);
enum openrfsfs_status fat32_backend_stat_path(
    enum openrfsfs_volume volume,
    const char *path,
    struct openrfsfs_stat *stat
);
enum openrfsfs_status fat32_backend_list(
    enum openrfsfs_volume volume,
    const char *path,
    struct openrfsfs_list_entry *entries,
    size_t capacity,
    size_t *entry_count
);
enum openrfsfs_status fat32_backend_create(
    enum openrfsfs_volume volume,
    const char *path,
    uint16_t mode
);
enum openrfsfs_status fat32_backend_truncate(
    enum openrfsfs_volume volume,
    const char *path,
    uint64_t size
);
enum openrfsfs_status fat32_backend_mkdir(
    enum openrfsfs_volume volume,
    const char *path
);
enum openrfsfs_status fat32_backend_rename(
    enum openrfsfs_volume volume,
    const char *source,
    const char *destination
);
enum openrfsfs_status fat32_backend_unlink(
    enum openrfsfs_volume volume,
    const char *path
);
enum openrfsfs_status fat32_backend_rmdir(
    enum openrfsfs_volume volume,
    const char *path
);
enum openrfsfs_status fat32_backend_link(
    enum openrfsfs_volume volume,
    const char *source,
    const char *destination
);

#endif
