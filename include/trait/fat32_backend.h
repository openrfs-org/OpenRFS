/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_FAT32_BACKEND_H
#define TRAIT_FAT32_BACKEND_H

#include <trait/fat32_fs.h>

/* Private VFS backend contract; kernel services use the traitfs_* VFS API. */
bool fat32_backend_self_test(size_t *completed_tests);
void fat32_backend_initialize(void);
enum traitfs_status fat32_backend_mount(enum traitfs_volume volume);
enum traitfs_status fat32_backend_unmount(enum traitfs_volume volume);
enum traitfs_status fat32_backend_sync(enum traitfs_volume volume);
struct traitfs_drive_info fat32_backend_drive(enum traitfs_volume volume);
uint64_t fat32_backend_completion_count(enum traitfs_volume volume);
enum traitfs_status fat32_backend_open(
    enum traitfs_volume volume,
    const char *path,
    enum traitfs_access access,
    traitfs_handle *handle
);
enum traitfs_status fat32_backend_close(traitfs_handle handle);
enum traitfs_status fat32_backend_read(
    traitfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    size_t *read_bytes
);
enum traitfs_status fat32_backend_pread(
    traitfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    uint64_t offset,
    size_t *read_bytes
);
enum traitfs_status fat32_backend_write(
    traitfs_handle handle,
    const uint8_t *source,
    size_t source_bytes,
    size_t *written_bytes
);
enum traitfs_status fat32_backend_seek(
    traitfs_handle handle,
    int64_t offset,
    enum traitfs_seek_origin origin,
    uint64_t *position
);
enum traitfs_status fat32_backend_stat_path(
    enum traitfs_volume volume,
    const char *path,
    struct traitfs_stat *stat
);
enum traitfs_status fat32_backend_list(
    enum traitfs_volume volume,
    const char *path,
    struct traitfs_list_entry *entries,
    size_t capacity,
    size_t *entry_count
);
enum traitfs_status fat32_backend_create(
    enum traitfs_volume volume,
    const char *path,
    uint16_t mode
);
enum traitfs_status fat32_backend_truncate(
    enum traitfs_volume volume,
    const char *path,
    uint64_t size
);
enum traitfs_status fat32_backend_mkdir(
    enum traitfs_volume volume,
    const char *path
);
enum traitfs_status fat32_backend_rename(
    enum traitfs_volume volume,
    const char *source,
    const char *destination
);
enum traitfs_status fat32_backend_unlink(
    enum traitfs_volume volume,
    const char *path
);
enum traitfs_status fat32_backend_rmdir(
    enum traitfs_volume volume,
    const char *path
);
enum traitfs_status fat32_backend_link(
    enum traitfs_volume volume,
    const char *source,
    const char *destination
);

#endif
