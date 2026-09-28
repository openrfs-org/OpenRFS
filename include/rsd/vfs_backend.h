/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_VFS_BACKEND_H
#define RSD_VFS_BACKEND_H

#include <rsd/fat32_fs.h>

/*
 * Concrete filesystem contract owned by the VFS. Backend handles and path
 * cookies never escape vfs.c; callers see only VFS file descriptions and
 * directory iterators.
 */
struct vfs_backend_ops {
    enum rsdfs_status (*open_options)(enum rsdfs_volume volume, const char *path,
        enum rsdfs_access access, uint8_t flags, uint16_t mode,
        rsdfs_handle *handle, struct rsdfs_stat *stat);
    enum rsdfs_status (*mount)(enum rsdfs_volume volume);
    enum rsdfs_status (*unmount)(enum rsdfs_volume volume);
    enum rsdfs_status (*sync)(enum rsdfs_volume volume);
    struct rsdfs_drive_info (*drive)(enum rsdfs_volume volume);
    uint64_t (*completion_count)(enum rsdfs_volume volume);
    enum rsdfs_status (*open)(enum rsdfs_volume volume, const char *path,
        enum rsdfs_access access, rsdfs_handle *handle);
    enum rsdfs_status (*close)(rsdfs_handle handle);
    enum rsdfs_status (*fsync)(rsdfs_handle handle);
    enum rsdfs_status (*fstat)(rsdfs_handle handle, struct rsdfs_stat *stat);
    enum rsdfs_status (*publish_file)(rsdfs_handle handle, const char *source, const char *destination);
    enum rsdfs_status (*unlink_held_file)(rsdfs_handle handle, const char *path);
    enum rsdfs_status (*read)(rsdfs_handle handle, uint8_t *destination,
        size_t capacity, size_t *read_bytes);
    enum rsdfs_status (*pread)(rsdfs_handle handle, uint8_t *destination,
        size_t capacity, uint64_t offset, size_t *read_bytes);
    enum rsdfs_status (*write)(rsdfs_handle handle, const uint8_t *source,
        size_t source_bytes, size_t *written_bytes);
    enum rsdfs_status (*seek)(rsdfs_handle handle, int64_t offset,
        enum rsdfs_seek_origin origin, uint64_t *position);
    enum rsdfs_status (*stat_path)(enum rsdfs_volume volume,
        const char *path, struct rsdfs_stat *stat);
    enum rsdfs_status (*lstat_path)(enum rsdfs_volume volume, const char *path, struct rsdfs_stat *stat);
    enum rsdfs_status (*list)(enum rsdfs_volume volume, const char *path,
        struct rsdfs_list_entry *entries, size_t capacity,
        size_t *entry_count);
    enum rsdfs_status (*directory_open)(enum rsdfs_volume volume,
        const char *path, rsdfs_handle *handle);
    enum rsdfs_status (*directory_read)(rsdfs_handle handle,
        struct rsdfs_list_entry *entry, bool *present);
    enum rsdfs_status (*directory_close)(rsdfs_handle handle);
    enum rsdfs_status (*create)(enum rsdfs_volume volume, const char *path,
        uint16_t mode);
    enum rsdfs_status (*truncate)(enum rsdfs_volume volume, const char *path,
        uint64_t size);
    enum rsdfs_status (*mkdir)(enum rsdfs_volume volume, const char *path);
    enum rsdfs_status (*mkdir_mode)(enum rsdfs_volume volume, const char *path, uint16_t mode);
    enum rsdfs_status (*rename)(enum rsdfs_volume volume, const char *source,
        const char *destination);
    enum rsdfs_status (*unlink)(enum rsdfs_volume volume, const char *path);
    enum rsdfs_status (*rmdir)(enum rsdfs_volume volume, const char *path);
    enum rsdfs_status (*link)(enum rsdfs_volume volume, const char *source,
        const char *destination);
    bool case_sensitive;
    /* Mutations validate all path components under their transaction lock.
     * A retained transaction must be retried without pre-reading its hidden view. */
    bool validates_mutation_paths;
    enum rsdfs_status (*remove)(enum rsdfs_volume volume, const char *path);
    enum rsdfs_status (*append)(rsdfs_handle handle, const uint8_t *source,
        size_t source_bytes, size_t *written_bytes);
    enum rsdfs_status (*rename_replace)(enum rsdfs_volume volume,
        const char *source, const char *destination);
    enum rsdfs_status (*symlink)(enum rsdfs_volume volume, const char *path,
        const char *target);
    enum rsdfs_status (*readlink)(enum rsdfs_volume volume, const char *path,
        uint8_t *output, size_t capacity, size_t *read_bytes);
    enum rsdfs_status (*chmod)(enum rsdfs_volume volume, const char *path, uint16_t mode);
    enum rsdfs_status (*ftruncate)(rsdfs_handle handle, uint64_t size);
    enum rsdfs_status (*set_times)(enum rsdfs_volume volume, const char *path,
        const struct rsdfs_times *times);
    enum rsdfs_status (*set_xattr)(enum rsdfs_volume volume, const char *path,
        const char *name, const uint8_t *value, size_t length, bool remove);
    enum rsdfs_status (*get_xattr)(enum rsdfs_volume volume, const char *path,
        const char *name, uint8_t *output, size_t capacity, size_t *length);
    /* Metadata and handle must come from the same protected inode lookup. */
    enum rsdfs_status (*open_with_stat)(enum rsdfs_volume volume, const char *path,
        enum rsdfs_access access, rsdfs_handle *handle, struct rsdfs_stat *stat);
    enum rsdfs_status (*directory_open_with_stat)(enum rsdfs_volume volume,
        const char *path, rsdfs_handle *handle, struct rsdfs_stat *stat);
};

#endif
