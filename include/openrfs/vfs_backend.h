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
    enum openrfsfs_status (*open_options)(enum openrfsfs_volume volume, const char *path,
        enum openrfsfs_access access, uint8_t flags, uint16_t mode,
        openrfsfs_handle *handle, struct openrfsfs_stat *stat);
    enum openrfsfs_status (*mount)(enum openrfsfs_volume volume);
    enum openrfsfs_status (*unmount)(enum openrfsfs_volume volume);
    enum openrfsfs_status (*sync)(enum openrfsfs_volume volume);
    struct openrfsfs_drive_info (*drive)(enum openrfsfs_volume volume);
    uint64_t (*completion_count)(enum openrfsfs_volume volume);
    enum openrfsfs_status (*open)(enum openrfsfs_volume volume, const char *path,
        enum openrfsfs_access access, openrfsfs_handle *handle);
    enum openrfsfs_status (*close)(openrfsfs_handle handle);
    enum openrfsfs_status (*fsync)(openrfsfs_handle handle);
    enum openrfsfs_status (*fstat)(openrfsfs_handle handle, struct openrfsfs_stat *stat);
    enum openrfsfs_status (*publish_file)(openrfsfs_handle handle, const char *source, const char *destination);
    enum openrfsfs_status (*unlink_held_file)(openrfsfs_handle handle, const char *path);
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
    enum openrfsfs_status (*lstat_path)(enum openrfsfs_volume volume, const char *path, struct openrfsfs_stat *stat);
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
    enum openrfsfs_status (*mkdir_mode)(enum openrfsfs_volume volume, const char *path, uint16_t mode);
    enum openrfsfs_status (*rename)(enum openrfsfs_volume volume, const char *source,
        const char *destination);
    enum openrfsfs_status (*unlink)(enum openrfsfs_volume volume, const char *path);
    enum openrfsfs_status (*rmdir)(enum openrfsfs_volume volume, const char *path);
    enum openrfsfs_status (*link)(enum openrfsfs_volume volume, const char *source,
        const char *destination);
    bool case_sensitive;
    /* Mutations validate all path components under their transaction lock.
     * A retained transaction must be retried without pre-reading its hidden view. */
    bool validates_mutation_paths;
    enum openrfsfs_status (*remove)(enum openrfsfs_volume volume, const char *path);
    enum openrfsfs_status (*append)(openrfsfs_handle handle, const uint8_t *source,
        size_t source_bytes, size_t *written_bytes);
    enum openrfsfs_status (*rename_replace)(enum openrfsfs_volume volume,
        const char *source, const char *destination);
    enum openrfsfs_status (*symlink)(enum openrfsfs_volume volume, const char *path,
        const char *target);
    enum openrfsfs_status (*readlink)(enum openrfsfs_volume volume, const char *path,
        uint8_t *output, size_t capacity, size_t *read_bytes);
    enum openrfsfs_status (*chmod)(enum openrfsfs_volume volume, const char *path, uint16_t mode);
    enum openrfsfs_status (*ftruncate)(openrfsfs_handle handle, uint64_t size);
    enum openrfsfs_status (*set_times)(enum openrfsfs_volume volume, const char *path,
        const struct openrfsfs_times *times);
    enum openrfsfs_status (*set_xattr)(enum openrfsfs_volume volume, const char *path,
        const char *name, const uint8_t *value, size_t length, bool remove);
    enum openrfsfs_status (*get_xattr)(enum openrfsfs_volume volume, const char *path,
        const char *name, uint8_t *output, size_t capacity, size_t *length);
    /* Metadata and handle must come from the same protected inode lookup. */
    enum openrfsfs_status (*open_with_stat)(enum openrfsfs_volume volume, const char *path,
        enum openrfsfs_access access, openrfsfs_handle *handle, struct openrfsfs_stat *stat);
    enum openrfsfs_status (*directory_open_with_stat)(enum openrfsfs_volume volume,
        const char *path, openrfsfs_handle *handle, struct openrfsfs_stat *stat);
};

const struct vfs_backend_ops *openrfsfs_data_backend_current(void);
enum openrfsfs_status openrfsfs_data_backend_replace(
    const struct vfs_backend_ops *expected,
    const struct vfs_backend_ops *replacement);

#endif
