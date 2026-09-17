/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_VFS_BACKEND_H
#define OPENRFS_VFS_BACKEND_H

#include <openrfs/fat32_fs.h>

/*
 * Concrete filesystem contract owned by the VFS. Backend handles and path
 * cookies never escape vfs.c; callers see only VFS file descriptions and
 * directory iterators.
 */
struct vfs_backend_ops {
    enum openrfsfs_status (*mount)(enum openrfsfs_volume volume);
    enum openrfsfs_status (*unmount)(enum openrfsfs_volume volume);
    enum openrfsfs_status (*sync)(enum openrfsfs_volume volume);
    struct openrfsfs_drive_info (*drive)(enum openrfsfs_volume volume);
    uint64_t (*completion_count)(enum openrfsfs_volume volume);
    enum openrfsfs_status (*open)(enum openrfsfs_volume volume, const char *path,
        enum openrfsfs_access access, openrfsfs_handle *handle);
    enum openrfsfs_status (*close)(openrfsfs_handle handle);
    enum openrfsfs_status (*read)(openrfsfs_handle handle, uint8_t *destination,
        size_t capacity, size_t *read_bytes);
    enum openrfsfs_status (*pread)(openrfsfs_handle handle, uint8_t *destination,
        size_t capacity, uint64_t offset, size_t *read_bytes);
    enum openrfsfs_status (*write)(openrfsfs_handle handle, const uint8_t *source,
        size_t source_bytes, size_t *written_bytes);
    enum openrfsfs_status (*seek)(openrfsfs_handle handle, int64_t offset,
        enum openrfsfs_seek_origin origin, uint64_t *position);
    enum openrfsfs_status (*stat_path)(enum openrfsfs_volume volume,
        const char *path, struct openrfsfs_stat *stat);
    enum openrfsfs_status (*list)(enum openrfsfs_volume volume, const char *path,
        struct openrfsfs_list_entry *entries, size_t capacity,
        size_t *entry_count);
    enum openrfsfs_status (*directory_open)(enum openrfsfs_volume volume,
        const char *path, openrfsfs_handle *handle);
    enum openrfsfs_status (*directory_read)(openrfsfs_handle handle,
        struct openrfsfs_list_entry *entry, bool *present);
    enum openrfsfs_status (*directory_close)(openrfsfs_handle handle);
    enum openrfsfs_status (*create)(enum openrfsfs_volume volume, const char *path,
        uint16_t mode);
    enum openrfsfs_status (*truncate)(enum openrfsfs_volume volume, const char *path,
        uint64_t size);
    enum openrfsfs_status (*mkdir)(enum openrfsfs_volume volume, const char *path);
    enum openrfsfs_status (*rename)(enum openrfsfs_volume volume, const char *source,
        const char *destination);
    enum openrfsfs_status (*unlink)(enum openrfsfs_volume volume, const char *path);
    enum openrfsfs_status (*rmdir)(enum openrfsfs_volume volume, const char *path);
    enum openrfsfs_status (*link)(enum openrfsfs_volume volume, const char *source,
        const char *destination);
    bool case_sensitive;
};

#endif
