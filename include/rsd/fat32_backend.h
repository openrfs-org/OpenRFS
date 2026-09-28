/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_FAT32_BACKEND_H
#define RSD_FAT32_BACKEND_H

#include <rsd/fat32_fs.h>

/* Private VFS backend contract; kernel services use the rsdfs_* VFS API. */
bool fat32_backend_self_test(size_t *completed_tests);
void fat32_backend_initialize(void);
enum rsdfs_status fat32_backend_mount(enum rsdfs_volume volume);
enum rsdfs_status fat32_backend_unmount(enum rsdfs_volume volume);
enum rsdfs_status fat32_backend_sync(enum rsdfs_volume volume);
struct rsdfs_drive_info fat32_backend_drive(enum rsdfs_volume volume);
uint64_t fat32_backend_completion_count(enum rsdfs_volume volume);
enum rsdfs_status fat32_backend_open(
    enum rsdfs_volume volume,
    const char *path,
    enum rsdfs_access access,
    rsdfs_handle *handle
);
enum rsdfs_status fat32_backend_close(rsdfs_handle handle);
enum rsdfs_status fat32_backend_read(
    rsdfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    size_t *read_bytes
);
enum rsdfs_status fat32_backend_pread(
    rsdfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    uint64_t offset,
    size_t *read_bytes
);
enum rsdfs_status fat32_backend_write(
    rsdfs_handle handle,
    const uint8_t *source,
    size_t source_bytes,
    size_t *written_bytes
);
enum rsdfs_status fat32_backend_seek(
    rsdfs_handle handle,
    int64_t offset,
    enum rsdfs_seek_origin origin,
    uint64_t *position
);
enum rsdfs_status fat32_backend_stat_path(
    enum rsdfs_volume volume,
    const char *path,
    struct rsdfs_stat *stat
);
enum rsdfs_status fat32_backend_list(
    enum rsdfs_volume volume,
    const char *path,
    struct rsdfs_list_entry *entries,
    size_t capacity,
    size_t *entry_count
);
enum rsdfs_status fat32_backend_create(
    enum rsdfs_volume volume,
    const char *path,
    uint16_t mode
);
enum rsdfs_status fat32_backend_truncate(
    enum rsdfs_volume volume,
    const char *path,
    uint64_t size
);
enum rsdfs_status fat32_backend_mkdir(
    enum rsdfs_volume volume,
    const char *path
);
enum rsdfs_status fat32_backend_rename(
    enum rsdfs_volume volume,
    const char *source,
    const char *destination
);
enum rsdfs_status fat32_backend_unlink(
    enum rsdfs_volume volume,
    const char *path
);
enum rsdfs_status fat32_backend_rmdir(
    enum rsdfs_volume volume,
    const char *path
);
enum rsdfs_status fat32_backend_link(
    enum rsdfs_volume volume,
    const char *source,
    const char *destination
);

#endif
