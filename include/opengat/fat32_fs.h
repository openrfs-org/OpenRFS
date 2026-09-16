/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_FAT32_FS_H
#define OPENGAT_FAT32_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <opengat/fat32.h>

#define OPENGATFS_MAX_MOUNTS 2U
#define OPENGATFS_MAX_HANDLES 64U
#define OPENGATFS_CACHE_ENTRIES 4U
#define OPENGATFS_MAX_PATH FAT32_PATH_BYTES
#define OPENGATFS_MAX_COMPONENT_BYTES 256U
#define OPENGATFS_MAX_DEPTH 16U
#define OPENGATFS_MAX_FILE_BYTES UINT32_C(16777216)
#define OPENGATFS_MAX_LIST_ENTRIES 64U

enum opengatfs_volume {
    OPENGATFS_VOLUME_SYSTEM = 0,
    OPENGATFS_VOLUME_DATA,
    OPENGATFS_VOLUME_COUNT
};

enum opengatfs_access {
    OPENGATFS_ACCESS_READ = 1U,
    OPENGATFS_ACCESS_WRITE = 2U,
    OPENGATFS_ACCESS_READ_WRITE = 3U
};

enum opengatfs_seek_origin {
    OPENGATFS_SEEK_START = 0,
    OPENGATFS_SEEK_CURRENT,
    OPENGATFS_SEEK_END
};

enum opengatfs_status {
    OPENGATFS_STATUS_OK = 0,
    OPENGATFS_STATUS_ABSENT,
    OPENGATFS_STATUS_CORRUPT,
    OPENGATFS_STATUS_IO,
    OPENGATFS_STATUS_INVALID_ARGUMENT,
    OPENGATFS_STATUS_NOT_MOUNTED,
    OPENGATFS_STATUS_ALREADY_MOUNTED,
    OPENGATFS_STATUS_READ_ONLY,
    OPENGATFS_STATUS_NOT_FOUND,
    OPENGATFS_STATUS_EXISTS,
    OPENGATFS_STATUS_NOT_DIRECTORY,
    OPENGATFS_STATUS_IS_DIRECTORY,
    OPENGATFS_STATUS_NOT_EMPTY,
    OPENGATFS_STATUS_BUSY,
    OPENGATFS_STATUS_NO_HANDLES,
    OPENGATFS_STATUS_STALE_HANDLE,
    OPENGATFS_STATUS_ACCESS,
    OPENGATFS_STATUS_RANGE,
    OPENGATFS_STATUS_FULL,
    OPENGATFS_STATUS_DIRECTORY_FULL,
    OPENGATFS_STATUS_NAME,
    OPENGATFS_STATUS_PATH,
    OPENGATFS_STATUS_WRITEBACK,
    OPENGATFS_STATUS_RESET,
    OPENGATFS_STATUS_COUNT
};

typedef uint64_t opengatfs_handle;
typedef uint64_t opengatfs_directory_handle;

struct opengatfs_stat {
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
};

struct opengatfs_list_entry {
    char name[OPENGATFS_MAX_COMPONENT_BYTES];
    uint64_t size;
    uint64_t object_id;
    uint16_t mode;
    uint8_t attributes;
    bool directory;
};

struct opengatfs_drive_info {
    enum opengatfs_volume volume;
    uint32_t volume_id;
    uint64_t total_bytes;
    uint64_t free_bytes;
    bool present;
    bool mounted;
    bool read_only;
    bool healthy;
};

bool opengatfs_self_test(size_t *completed_tests);
void opengatfs_initialize(void);
enum opengatfs_status opengatfs_mount(enum opengatfs_volume volume);
enum opengatfs_status opengatfs_unmount(enum opengatfs_volume volume);
enum opengatfs_status opengatfs_sync(enum opengatfs_volume volume);
struct opengatfs_drive_info opengatfs_drive(enum opengatfs_volume volume);
uint64_t opengatfs_completion_count(enum opengatfs_volume volume);
enum opengatfs_status opengatfs_open(
    enum opengatfs_volume volume,
    const char *path,
    enum opengatfs_access access,
    opengatfs_handle *handle
);
enum opengatfs_status opengatfs_close(opengatfs_handle handle);
enum opengatfs_status opengatfs_read(
    opengatfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    size_t *read_bytes
);
enum opengatfs_status opengatfs_pread(
    opengatfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    uint64_t offset,
    size_t *read_bytes
);
enum opengatfs_status opengatfs_write(
    opengatfs_handle handle,
    const uint8_t *source,
    size_t source_bytes,
    size_t *written_bytes
);
enum opengatfs_status opengatfs_seek(
    opengatfs_handle handle,
    int64_t offset,
    enum opengatfs_seek_origin origin,
    uint64_t *position
);
enum opengatfs_status opengatfs_stat_path(
    enum opengatfs_volume volume,
    const char *path,
    struct opengatfs_stat *stat
);
enum opengatfs_status opengatfs_list(
    enum opengatfs_volume volume,
    const char *path,
    struct opengatfs_list_entry *entries,
    size_t capacity,
    size_t *entry_count
);
enum opengatfs_status opengatfs_directory_open(
    enum opengatfs_volume volume,
    const char *path,
    opengatfs_directory_handle *handle
);
enum opengatfs_status opengatfs_directory_read(
    opengatfs_directory_handle handle,
    struct opengatfs_list_entry *entry,
    bool *present
);
enum opengatfs_status opengatfs_directory_close(opengatfs_directory_handle handle);
enum opengatfs_status opengatfs_create(enum opengatfs_volume volume, const char *path);
enum opengatfs_status opengatfs_create_mode(enum opengatfs_volume volume,
    const char *path, uint16_t mode);
enum opengatfs_status opengatfs_truncate(
    enum opengatfs_volume volume,
    const char *path,
    uint64_t size
);
enum opengatfs_status opengatfs_mkdir(enum opengatfs_volume volume, const char *path);
enum opengatfs_status opengatfs_rename(
    enum opengatfs_volume volume,
    const char *source,
    const char *destination
);
enum opengatfs_status opengatfs_unlink(enum opengatfs_volume volume, const char *path);
enum opengatfs_status opengatfs_rmdir(enum opengatfs_volume volume, const char *path);
enum opengatfs_status opengatfs_link(
    enum opengatfs_volume volume,
    const char *source,
    const char *destination
);
const char *opengatfs_status_string(enum opengatfs_status status);

#endif
