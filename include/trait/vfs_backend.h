/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_VFS_BACKEND_H
#define TRAIT_VFS_BACKEND_H

#include <trait/fat32_fs.h>

/*
 * Concrete filesystem contract owned by the VFS. Backend handles and path
 * cookies never escape vfs.c; callers see only VFS file descriptions and
 * directory iterators.
 */
struct vfs_backend_ops {
    enum traitfs_status (*mount)(enum traitfs_volume volume);
    enum traitfs_status (*unmount)(enum traitfs_volume volume);
    enum traitfs_status (*sync)(enum traitfs_volume volume);
    struct traitfs_drive_info (*drive)(enum traitfs_volume volume);
    uint64_t (*completion_count)(enum traitfs_volume volume);
    enum traitfs_status (*open)(enum traitfs_volume volume, const char *path,
        enum traitfs_access access, traitfs_handle *handle);
    enum traitfs_status (*close)(traitfs_handle handle);
    enum traitfs_status (*read)(traitfs_handle handle, uint8_t *destination,
        size_t capacity, size_t *read_bytes);
    enum traitfs_status (*pread)(traitfs_handle handle, uint8_t *destination,
        size_t capacity, uint64_t offset, size_t *read_bytes);
    enum traitfs_status (*write)(traitfs_handle handle, const uint8_t *source,
        size_t source_bytes, size_t *written_bytes);
    enum traitfs_status (*seek)(traitfs_handle handle, int64_t offset,
        enum traitfs_seek_origin origin, uint64_t *position);
    enum traitfs_status (*stat_path)(enum traitfs_volume volume,
        const char *path, struct traitfs_stat *stat);
    enum traitfs_status (*list)(enum traitfs_volume volume, const char *path,
        struct traitfs_list_entry *entries, size_t capacity,
        size_t *entry_count);
    enum traitfs_status (*directory_open)(enum traitfs_volume volume,
        const char *path, traitfs_handle *handle);
    enum traitfs_status (*directory_read)(traitfs_handle handle,
        struct traitfs_list_entry *entry, bool *present);
    enum traitfs_status (*directory_close)(traitfs_handle handle);
    enum traitfs_status (*create)(enum traitfs_volume volume, const char *path,
        uint16_t mode);
    enum traitfs_status (*truncate)(enum traitfs_volume volume, const char *path,
        uint64_t size);
    enum traitfs_status (*mkdir)(enum traitfs_volume volume, const char *path);
    enum traitfs_status (*rename)(enum traitfs_volume volume, const char *source,
        const char *destination);
    enum traitfs_status (*unlink)(enum traitfs_volume volume, const char *path);
    enum traitfs_status (*rmdir)(enum traitfs_volume volume, const char *path);
    enum traitfs_status (*link)(enum traitfs_volume volume, const char *source,
        const char *destination);
    bool case_sensitive;
};

#endif
