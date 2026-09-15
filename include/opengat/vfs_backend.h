/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_VFS_BACKEND_H
#define OPENGAT_VFS_BACKEND_H

#include <opengat/fat32_fs.h>

/*
 * Concrete filesystem contract owned by the VFS. Backend handles and path
 * cookies never escape vfs.c; callers see only VFS file descriptions and
 * directory iterators.
 */
struct vfs_backend_ops {
    enum opengatfs_status (*mount)(enum opengatfs_volume volume);
    enum opengatfs_status (*unmount)(enum opengatfs_volume volume);
    enum opengatfs_status (*sync)(enum opengatfs_volume volume);
    struct opengatfs_drive_info (*drive)(enum opengatfs_volume volume);
    uint64_t (*completion_count)(enum opengatfs_volume volume);
    enum opengatfs_status (*open)(enum opengatfs_volume volume, const char *path,
        enum opengatfs_access access, opengatfs_handle *handle);
    enum opengatfs_status (*close)(opengatfs_handle handle);
    enum opengatfs_status (*read)(opengatfs_handle handle, uint8_t *destination,
        size_t capacity, size_t *read_bytes);
    enum opengatfs_status (*pread)(opengatfs_handle handle, uint8_t *destination,
        size_t capacity, uint64_t offset, size_t *read_bytes);
    enum opengatfs_status (*write)(opengatfs_handle handle, const uint8_t *source,
        size_t source_bytes, size_t *written_bytes);
    enum opengatfs_status (*seek)(opengatfs_handle handle, int64_t offset,
        enum opengatfs_seek_origin origin, uint64_t *position);
    enum opengatfs_status (*stat_path)(enum opengatfs_volume volume,
        const char *path, struct opengatfs_stat *stat);
    enum opengatfs_status (*list)(enum opengatfs_volume volume, const char *path,
        struct opengatfs_list_entry *entries, size_t capacity,
        size_t *entry_count);
    enum opengatfs_status (*directory_open)(enum opengatfs_volume volume,
        const char *path, opengatfs_handle *handle);
    enum opengatfs_status (*directory_read)(opengatfs_handle handle,
        struct opengatfs_list_entry *entry, bool *present);
    enum opengatfs_status (*directory_close)(opengatfs_handle handle);
    enum opengatfs_status (*create)(enum opengatfs_volume volume, const char *path,
        uint16_t mode);
    enum opengatfs_status (*truncate)(enum opengatfs_volume volume, const char *path,
        uint64_t size);
    enum opengatfs_status (*mkdir)(enum opengatfs_volume volume, const char *path);
    enum opengatfs_status (*rename)(enum opengatfs_volume volume, const char *source,
        const char *destination);
    enum opengatfs_status (*unlink)(enum opengatfs_volume volume, const char *path);
    enum opengatfs_status (*rmdir)(enum opengatfs_volume volume, const char *path);
    enum opengatfs_status (*link)(enum opengatfs_volume volume, const char *source,
        const char *destination);
    bool case_sensitive;
};

#endif
