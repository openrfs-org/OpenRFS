/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_FAT32_BACKEND_H
#define OPENGAT_FAT32_BACKEND_H

#include <opengat/fat32_fs.h>

/* Private VFS backend contract; kernel services use the opengatfs_* VFS API. */
bool fat32_backend_self_test(size_t *completed_tests);
void fat32_backend_initialize(void);
enum opengatfs_status fat32_backend_mount(enum opengatfs_volume volume);
enum opengatfs_status fat32_backend_unmount(enum opengatfs_volume volume);
enum opengatfs_status fat32_backend_sync(enum opengatfs_volume volume);
struct opengatfs_drive_info fat32_backend_drive(enum opengatfs_volume volume);
uint64_t fat32_backend_completion_count(enum opengatfs_volume volume);
enum opengatfs_status fat32_backend_open(
    enum opengatfs_volume volume,
    const char *path,
    enum opengatfs_access access,
    opengatfs_handle *handle
);
enum opengatfs_status fat32_backend_close(opengatfs_handle handle);
enum opengatfs_status fat32_backend_read(
    opengatfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    size_t *read_bytes
);
enum opengatfs_status fat32_backend_pread(
    opengatfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    uint64_t offset,
    size_t *read_bytes
);
enum opengatfs_status fat32_backend_write(
    opengatfs_handle handle,
    const uint8_t *source,
    size_t source_bytes,
    size_t *written_bytes
);
enum opengatfs_status fat32_backend_seek(
    opengatfs_handle handle,
    int64_t offset,
    enum opengatfs_seek_origin origin,
    uint64_t *position
);
enum opengatfs_status fat32_backend_stat_path(
    enum opengatfs_volume volume,
    const char *path,
    struct opengatfs_stat *stat
);
enum opengatfs_status fat32_backend_list(
    enum opengatfs_volume volume,
    const char *path,
    struct opengatfs_list_entry *entries,
    size_t capacity,
    size_t *entry_count
);
enum opengatfs_status fat32_backend_create(
    enum opengatfs_volume volume,
    const char *path,
    uint16_t mode
);
enum opengatfs_status fat32_backend_truncate(
    enum opengatfs_volume volume,
    const char *path,
    uint64_t size
);
enum opengatfs_status fat32_backend_mkdir(
    enum opengatfs_volume volume,
    const char *path
);
enum opengatfs_status fat32_backend_rename(
    enum opengatfs_volume volume,
    const char *source,
    const char *destination
);
enum opengatfs_status fat32_backend_unlink(
    enum opengatfs_volume volume,
    const char *path
);
enum opengatfs_status fat32_backend_rmdir(
    enum opengatfs_volume volume,
    const char *path
);
enum opengatfs_status fat32_backend_link(
    enum opengatfs_volume volume,
    const char *source,
    const char *destination
);

#endif
