/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_FAT32_FS_H
#define RSD_FAT32_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <rsd/fat32.h>

#define RSDFS_MAX_MOUNTS 2U
#define RSDFS_MAX_HANDLES 64U
#define RSDFS_CACHE_ENTRIES 4U
#define RSDFS_MAX_PATH FAT32_PATH_BYTES
#define RSDFS_MAX_COMPONENT_BYTES 256U
#define RSDFS_MAX_DEPTH 16U
#define RSDFS_MAX_FILE_BYTES UINT32_C(16777216)
#define RSDFS_MAX_LIST_ENTRIES 64U

enum rsdfs_volume {
    RSDFS_VOLUME_SYSTEM = 0,
    RSDFS_VOLUME_DATA,
    RSDFS_VOLUME_COUNT
};

enum rsdfs_access {
    RSDFS_ACCESS_READ = 1U,
    RSDFS_ACCESS_WRITE = 2U,
    RSDFS_ACCESS_READ_WRITE = 3U
};

#define RSDFS_OPEN_CREATE 1U
#define RSDFS_OPEN_TRUNCATE 2U
#define RSDFS_OPEN_EXCLUSIVE 4U

enum rsdfs_seek_origin {
    RSDFS_SEEK_START = 0,
    RSDFS_SEEK_CURRENT,
    RSDFS_SEEK_END
};

enum rsdfs_status {
    RSDFS_STATUS_OK = 0,
    RSDFS_STATUS_ABSENT,
    RSDFS_STATUS_CORRUPT,
    RSDFS_STATUS_IO,
    RSDFS_STATUS_INVALID_ARGUMENT,
    RSDFS_STATUS_NOT_MOUNTED,
    RSDFS_STATUS_ALREADY_MOUNTED,
    RSDFS_STATUS_READ_ONLY,
    RSDFS_STATUS_NOT_FOUND,
    RSDFS_STATUS_EXISTS,
    RSDFS_STATUS_NOT_DIRECTORY,
    RSDFS_STATUS_IS_DIRECTORY,
    RSDFS_STATUS_NOT_EMPTY,
    RSDFS_STATUS_BUSY,
    RSDFS_STATUS_NO_HANDLES,
    RSDFS_STATUS_STALE_HANDLE,
    RSDFS_STATUS_ACCESS,
    RSDFS_STATUS_RANGE,
    RSDFS_STATUS_FULL,
    RSDFS_STATUS_DIRECTORY_FULL,
    RSDFS_STATUS_NAME,
    RSDFS_STATUS_PATH,
    RSDFS_STATUS_WRITEBACK,
    RSDFS_STATUS_RESET,
    RSDFS_STATUS_NAME_TOO_LONG,
    RSDFS_STATUS_SYMLINK_LOOP,
    RSDFS_STATUS_COUNT
};

typedef uint64_t rsdfs_handle;
typedef uint64_t rsdfs_directory_handle;

struct rsdfs_stat {
    uint64_t size;
    uint64_t object_id;
    uint32_t first_cluster;
    uint32_t cluster_count;
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint16_t links;
    uint8_t attributes;
    bool directory;
    bool read_only;
    int64_t atime_seconds;
    int64_t mtime_seconds;
    int64_t ctime_seconds;
    uint32_t atime_nanos;
    uint32_t mtime_nanos;
    uint32_t ctime_nanos;
};

struct rsdfs_times {
    uint64_t atime_seconds;
    uint64_t mtime_seconds;
    uint32_t atime_nanos;
    uint32_t mtime_nanos;
};

struct rsdfs_list_entry {
    char name[RSDFS_MAX_COMPONENT_BYTES];
    uint64_t size;
    uint64_t object_id;
    uint16_t mode;
    uint8_t attributes;
    bool directory;
};

struct rsdfs_drive_info {
    enum rsdfs_volume volume;
    uint32_t volume_id;
    uint64_t total_bytes;
    uint64_t free_bytes;
    bool present;
    bool mounted;
    bool read_only;
    bool healthy;
};

bool rsdfs_self_test(size_t *completed_tests);
void rsdfs_initialize(void);
/* Quiescent VFS census after all mounts have been cleanly released. */
bool rsdfs_resources_released(void);
enum rsdfs_status rsdfs_mount(enum rsdfs_volume volume);
enum rsdfs_status rsdfs_unmount(enum rsdfs_volume volume);
enum rsdfs_status rsdfs_sync(enum rsdfs_volume volume);
struct rsdfs_drive_info rsdfs_drive(enum rsdfs_volume volume);
uint64_t rsdfs_completion_count(enum rsdfs_volume volume);
enum rsdfs_status rsdfs_open(
    enum rsdfs_volume volume,
    const char *path,
    enum rsdfs_access access,
    rsdfs_handle *handle
);
enum rsdfs_status rsdfs_open_options(enum rsdfs_volume volume, const char *path,
    enum rsdfs_access access, uint8_t flags, uint16_t mode, rsdfs_handle *handle);
enum rsdfs_status rsdfs_close(rsdfs_handle handle);
/* Report ownership separately from writeback status for enclosing registries. */
enum rsdfs_status rsdfs_close_report(rsdfs_handle handle, bool *consumed);
enum rsdfs_status rsdfs_fsync(rsdfs_handle handle);
enum rsdfs_status rsdfs_fstat(rsdfs_handle handle, struct rsdfs_stat *stat);
enum rsdfs_status rsdfs_publish_file(rsdfs_handle handle, const char *source, const char *destination);
/* Remove path only if it still names this writable handle's regular inode. */
enum rsdfs_status rsdfs_unlink_held_file(rsdfs_handle handle, const char *path);
enum rsdfs_status rsdfs_read(
    rsdfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    size_t *read_bytes
);
enum rsdfs_status rsdfs_pread(
    rsdfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    uint64_t offset,
    size_t *read_bytes
);
enum rsdfs_status rsdfs_write(
    rsdfs_handle handle,
    const uint8_t *source,
    size_t source_bytes,
    size_t *written_bytes
);
enum rsdfs_status rsdfs_seek(
    rsdfs_handle handle,
    int64_t offset,
    enum rsdfs_seek_origin origin,
    uint64_t *position
);
enum rsdfs_status rsdfs_lstat_path(enum rsdfs_volume volume, const char *path, struct rsdfs_stat *stat);
enum rsdfs_status rsdfs_stat_path(
    enum rsdfs_volume volume,
    const char *path,
    struct rsdfs_stat *stat
);
enum rsdfs_status rsdfs_list(
    enum rsdfs_volume volume,
    const char *path,
    struct rsdfs_list_entry *entries,
    size_t capacity,
    size_t *entry_count
);
enum rsdfs_status rsdfs_directory_open(
    enum rsdfs_volume volume,
    const char *path,
    rsdfs_directory_handle *handle
);
enum rsdfs_status rsdfs_directory_read(
    rsdfs_directory_handle handle,
    struct rsdfs_list_entry *entry,
    bool *present
);
enum rsdfs_status rsdfs_directory_close(rsdfs_directory_handle handle);
/* Report ownership separately from backend close/writeback status. */
enum rsdfs_status rsdfs_directory_close_report(
    rsdfs_directory_handle handle, bool *consumed);
enum rsdfs_status rsdfs_create(enum rsdfs_volume volume, const char *path);
enum rsdfs_status rsdfs_create_mode(enum rsdfs_volume volume,
    const char *path, uint16_t mode);
enum rsdfs_status rsdfs_truncate(
    enum rsdfs_volume volume,
    const char *path,
    uint64_t size
);
enum rsdfs_status rsdfs_mkdir(enum rsdfs_volume volume, const char *path);
enum rsdfs_status rsdfs_mkdir_mode(enum rsdfs_volume volume, const char *path, uint16_t mode);
enum rsdfs_status rsdfs_rename(
    enum rsdfs_volume volume,
    const char *source,
    const char *destination
);
enum rsdfs_status rsdfs_unlink(enum rsdfs_volume volume, const char *path);
enum rsdfs_status rsdfs_remove(enum rsdfs_volume volume, const char *path);
enum rsdfs_status rsdfs_rmdir(enum rsdfs_volume volume, const char *path);
enum rsdfs_status rsdfs_link(
    enum rsdfs_volume volume,
    const char *source,
    const char *destination
);
const char *rsdfs_status_string(enum rsdfs_status status);
enum rsdfs_status rsdfs_rename_replace(enum rsdfs_volume volume,
    const char *source, const char *destination);
bool rsdfs_has_atomic_replace(enum rsdfs_volume volume);
enum rsdfs_status rsdfs_set_append(rsdfs_handle handle, bool append);
enum rsdfs_status rsdfs_ftruncate(rsdfs_handle handle, uint64_t size);
enum rsdfs_status rsdfs_set_times(enum rsdfs_volume volume, const char *path,
    const struct rsdfs_times *times);
enum rsdfs_status rsdfs_symlink(enum rsdfs_volume volume,
    const char *path, const char *target);
/* Copies at most capacity literal target bytes, without adding a NUL. */
enum rsdfs_status rsdfs_readlink(enum rsdfs_volume volume,
    const char *path, uint8_t *output, size_t capacity, size_t *read_bytes);

enum rsdfs_status rsdfs_chmod(enum rsdfs_volume volume,
    const char *path, uint16_t mode);
enum rsdfs_status rsdfs_set_xattr(enum rsdfs_volume volume,
    const char *path, const char *name, const uint8_t *value, size_t length, bool remove);
enum rsdfs_status rsdfs_get_xattr(enum rsdfs_volume volume,
    const char *path, const char *name, uint8_t *output, size_t capacity, size_t *length);

#endif
