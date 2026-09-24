/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_FAT32_FS_H
#define OPENRFS_FAT32_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/fat32.h>

#define OPENRFSFS_MAX_MOUNTS 2U
#define OPENRFSFS_MAX_HANDLES 64U
#define OPENRFSFS_CACHE_ENTRIES 4U
#define OPENRFSFS_MAX_PATH FAT32_PATH_BYTES
#define OPENRFSFS_MAX_COMPONENT_BYTES 256U
#define OPENRFSFS_MAX_DEPTH 16U
#define OPENRFSFS_MAX_FILE_BYTES UINT32_C(16777216)
#define OPENRFSFS_MAX_LIST_ENTRIES 64U

enum openrfsfs_volume {
    OPENRFSFS_VOLUME_SYSTEM = 0,
    OPENRFSFS_VOLUME_DATA,
    OPENRFSFS_VOLUME_COUNT
};

enum openrfsfs_access {
    OPENRFSFS_ACCESS_READ = 1U,
    OPENRFSFS_ACCESS_WRITE = 2U,
    OPENRFSFS_ACCESS_READ_WRITE = 3U
};

#define OPENRFSFS_OPEN_CREATE 1U
#define OPENRFSFS_OPEN_TRUNCATE 2U
#define OPENRFSFS_OPEN_EXCLUSIVE 4U

enum openrfsfs_seek_origin {
    OPENRFSFS_SEEK_START = 0,
    OPENRFSFS_SEEK_CURRENT,
    OPENRFSFS_SEEK_END
};

enum openrfsfs_status {
    OPENRFSFS_STATUS_OK = 0,
    OPENRFSFS_STATUS_ABSENT,
    OPENRFSFS_STATUS_CORRUPT,
    OPENRFSFS_STATUS_IO,
    OPENRFSFS_STATUS_INVALID_ARGUMENT,
    OPENRFSFS_STATUS_NOT_MOUNTED,
    OPENRFSFS_STATUS_ALREADY_MOUNTED,
    OPENRFSFS_STATUS_READ_ONLY,
    OPENRFSFS_STATUS_NOT_FOUND,
    OPENRFSFS_STATUS_EXISTS,
    OPENRFSFS_STATUS_NOT_DIRECTORY,
    OPENRFSFS_STATUS_IS_DIRECTORY,
    OPENRFSFS_STATUS_NOT_EMPTY,
    OPENRFSFS_STATUS_BUSY,
    OPENRFSFS_STATUS_NO_HANDLES,
    OPENRFSFS_STATUS_STALE_HANDLE,
    OPENRFSFS_STATUS_ACCESS,
    OPENRFSFS_STATUS_RANGE,
    OPENRFSFS_STATUS_FULL,
    OPENRFSFS_STATUS_DIRECTORY_FULL,
    OPENRFSFS_STATUS_NAME,
    OPENRFSFS_STATUS_PATH,
    OPENRFSFS_STATUS_WRITEBACK,
    OPENRFSFS_STATUS_RESET,
    OPENRFSFS_STATUS_NAME_TOO_LONG,
    OPENRFSFS_STATUS_SYMLINK_LOOP,
    OPENRFSFS_STATUS_COUNT
};

typedef uint64_t openrfsfs_handle;
typedef uint64_t openrfsfs_directory_handle;

struct openrfsfs_stat {
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

struct openrfsfs_times {
    uint64_t atime_seconds;
    uint64_t mtime_seconds;
    uint32_t atime_nanos;
    uint32_t mtime_nanos;
};

struct openrfsfs_list_entry {
    char name[OPENRFSFS_MAX_COMPONENT_BYTES];
    uint64_t size;
    uint64_t object_id;
    uint16_t mode;
    uint8_t attributes;
    bool directory;
};

enum openrfsfs_filesystem {
    OPENRFSFS_FILESYSTEM_UNKNOWN = 0,
    OPENRFSFS_FILESYSTEM_FAT32,
    OPENRFSFS_FILESYSTEM_EXT4PLUS
};

struct openrfsfs_drive_info {
    enum openrfsfs_volume volume;
    enum openrfsfs_filesystem filesystem;
    uint32_t volume_id;
    uint64_t total_bytes;
    uint64_t free_bytes;
    bool present;
    bool mounted;
    bool read_only;
    bool healthy;
};

bool openrfsfs_self_test(size_t *completed_tests);
void openrfsfs_initialize(void);
/* Monotonic ordinary-boot Data gate; credential records remain reachable. */
void openrfsfs_data_login_lock_enable(bool (*session_active)(void));
/* Quiescent VFS census after all mounts have been cleanly released. */
bool openrfsfs_resources_released(void);
enum openrfsfs_status openrfsfs_mount(enum openrfsfs_volume volume);
enum openrfsfs_status openrfsfs_unmount(enum openrfsfs_volume volume);
enum openrfsfs_status openrfsfs_sync(enum openrfsfs_volume volume);
struct openrfsfs_drive_info openrfsfs_drive(enum openrfsfs_volume volume);
uint64_t openrfsfs_completion_count(enum openrfsfs_volume volume);
enum openrfsfs_status openrfsfs_open(
    enum openrfsfs_volume volume,
    const char *path,
    enum openrfsfs_access access,
    openrfsfs_handle *handle
);
enum openrfsfs_status openrfsfs_open_options(enum openrfsfs_volume volume, const char *path,
    enum openrfsfs_access access, uint8_t flags, uint16_t mode, openrfsfs_handle *handle);
enum openrfsfs_status openrfsfs_close(openrfsfs_handle handle);
/* Report ownership separately from writeback status for enclosing registries. */
enum openrfsfs_status openrfsfs_close_report(openrfsfs_handle handle, bool *consumed);
enum openrfsfs_status openrfsfs_fsync(openrfsfs_handle handle);
enum openrfsfs_status openrfsfs_fstat(openrfsfs_handle handle, struct openrfsfs_stat *stat);
enum openrfsfs_status openrfsfs_publish_file(openrfsfs_handle handle, const char *source, const char *destination);
/* Remove path only if it still names this writable handle's regular inode. */
enum openrfsfs_status openrfsfs_unlink_held_file(openrfsfs_handle handle, const char *path);
enum openrfsfs_status openrfsfs_read(
    openrfsfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    size_t *read_bytes
);
enum openrfsfs_status openrfsfs_pread(
    openrfsfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    uint64_t offset,
    size_t *read_bytes
);
enum openrfsfs_status openrfsfs_write(
    openrfsfs_handle handle,
    const uint8_t *source,
    size_t source_bytes,
    size_t *written_bytes
);
enum openrfsfs_status openrfsfs_seek(
    openrfsfs_handle handle,
    int64_t offset,
    enum openrfsfs_seek_origin origin,
    uint64_t *position
);
enum openrfsfs_status openrfsfs_lstat_path(enum openrfsfs_volume volume, const char *path, struct openrfsfs_stat *stat);
enum openrfsfs_status openrfsfs_stat_path(
    enum openrfsfs_volume volume,
    const char *path,
    struct openrfsfs_stat *stat
);
enum openrfsfs_status openrfsfs_list(
    enum openrfsfs_volume volume,
    const char *path,
    struct openrfsfs_list_entry *entries,
    size_t capacity,
    size_t *entry_count
);
enum openrfsfs_status openrfsfs_directory_open(
    enum openrfsfs_volume volume,
    const char *path,
    openrfsfs_directory_handle *handle
);
enum openrfsfs_status openrfsfs_directory_read(
    openrfsfs_directory_handle handle,
    struct openrfsfs_list_entry *entry,
    bool *present
);
enum openrfsfs_status openrfsfs_directory_close(openrfsfs_directory_handle handle);
/* Report ownership separately from backend close/writeback status. */
enum openrfsfs_status openrfsfs_directory_close_report(
    openrfsfs_directory_handle handle, bool *consumed);
enum openrfsfs_status openrfsfs_create(enum openrfsfs_volume volume, const char *path);
enum openrfsfs_status openrfsfs_create_mode(enum openrfsfs_volume volume,
    const char *path, uint16_t mode);
enum openrfsfs_status openrfsfs_truncate(
    enum openrfsfs_volume volume,
    const char *path,
    uint64_t size
);
enum openrfsfs_status openrfsfs_mkdir(enum openrfsfs_volume volume, const char *path);
enum openrfsfs_status openrfsfs_mkdir_mode(enum openrfsfs_volume volume, const char *path, uint16_t mode);
enum openrfsfs_status openrfsfs_rename(
    enum openrfsfs_volume volume,
    const char *source,
    const char *destination
);
enum openrfsfs_status openrfsfs_unlink(enum openrfsfs_volume volume, const char *path);
enum openrfsfs_status openrfsfs_remove(enum openrfsfs_volume volume, const char *path);
enum openrfsfs_status openrfsfs_rmdir(enum openrfsfs_volume volume, const char *path);
enum openrfsfs_status openrfsfs_link(
    enum openrfsfs_volume volume,
    const char *source,
    const char *destination
);
const char *openrfsfs_status_string(enum openrfsfs_status status);
enum openrfsfs_status openrfsfs_rename_replace(enum openrfsfs_volume volume,
    const char *source, const char *destination);
bool openrfsfs_has_atomic_replace(enum openrfsfs_volume volume);
enum openrfsfs_status openrfsfs_set_append(openrfsfs_handle handle, bool append);
enum openrfsfs_status openrfsfs_ftruncate(openrfsfs_handle handle, uint64_t size);
enum openrfsfs_status openrfsfs_set_times(enum openrfsfs_volume volume, const char *path,
    const struct openrfsfs_times *times);
enum openrfsfs_status openrfsfs_symlink(enum openrfsfs_volume volume,
    const char *path, const char *target);
/* Copies at most capacity literal target bytes, without adding a NUL. */
enum openrfsfs_status openrfsfs_readlink(enum openrfsfs_volume volume,
    const char *path, uint8_t *output, size_t capacity, size_t *read_bytes);

enum openrfsfs_status openrfsfs_chmod(enum openrfsfs_volume volume,
    const char *path, uint16_t mode);
enum openrfsfs_status openrfsfs_set_xattr(enum openrfsfs_volume volume,
    const char *path, const char *name, const uint8_t *value, size_t length, bool remove);
enum openrfsfs_status openrfsfs_get_xattr(enum openrfsfs_volume volume,
    const char *path, const char *name, uint8_t *output, size_t capacity, size_t *length);

#endif
