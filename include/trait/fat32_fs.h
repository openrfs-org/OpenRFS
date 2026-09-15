/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_FAT32_FS_H
#define TRAIT_FAT32_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <trait/fat32.h>

#define TRAITFS_MAX_MOUNTS 2U
#define TRAITFS_MAX_HANDLES 64U
#define TRAITFS_CACHE_ENTRIES 4U
#define TRAITFS_MAX_PATH FAT32_PATH_BYTES
#define TRAITFS_MAX_COMPONENT_BYTES 256U
#define TRAITFS_MAX_DEPTH 16U
#define TRAITFS_MAX_FILE_BYTES UINT32_C(16777216)
#define TRAITFS_MAX_LIST_ENTRIES 64U

enum traitfs_volume {
    TRAITFS_VOLUME_SYSTEM = 0,
    TRAITFS_VOLUME_DATA,
    TRAITFS_VOLUME_COUNT
};

enum traitfs_access {
    TRAITFS_ACCESS_READ = 1U,
    TRAITFS_ACCESS_WRITE = 2U,
    TRAITFS_ACCESS_READ_WRITE = 3U
};

enum traitfs_seek_origin {
    TRAITFS_SEEK_START = 0,
    TRAITFS_SEEK_CURRENT,
    TRAITFS_SEEK_END
};

enum traitfs_status {
    TRAITFS_STATUS_OK = 0,
    TRAITFS_STATUS_ABSENT,
    TRAITFS_STATUS_CORRUPT,
    TRAITFS_STATUS_IO,
    TRAITFS_STATUS_INVALID_ARGUMENT,
    TRAITFS_STATUS_NOT_MOUNTED,
    TRAITFS_STATUS_ALREADY_MOUNTED,
    TRAITFS_STATUS_READ_ONLY,
    TRAITFS_STATUS_NOT_FOUND,
    TRAITFS_STATUS_EXISTS,
    TRAITFS_STATUS_NOT_DIRECTORY,
    TRAITFS_STATUS_IS_DIRECTORY,
    TRAITFS_STATUS_NOT_EMPTY,
    TRAITFS_STATUS_BUSY,
    TRAITFS_STATUS_NO_HANDLES,
    TRAITFS_STATUS_STALE_HANDLE,
    TRAITFS_STATUS_ACCESS,
    TRAITFS_STATUS_RANGE,
    TRAITFS_STATUS_FULL,
    TRAITFS_STATUS_DIRECTORY_FULL,
    TRAITFS_STATUS_NAME,
    TRAITFS_STATUS_PATH,
    TRAITFS_STATUS_WRITEBACK,
    TRAITFS_STATUS_RESET,
    TRAITFS_STATUS_COUNT
};

typedef uint64_t traitfs_handle;
typedef uint64_t traitfs_directory_handle;

struct traitfs_stat {
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

struct traitfs_list_entry {
    char name[TRAITFS_MAX_COMPONENT_BYTES];
    uint64_t size;
    uint64_t object_id;
    uint16_t mode;
    uint8_t attributes;
    bool directory;
};

struct traitfs_drive_info {
    enum traitfs_volume volume;
    uint32_t volume_id;
    uint64_t total_bytes;
    uint64_t free_bytes;
    bool present;
    bool mounted;
    bool read_only;
    bool healthy;
};

bool traitfs_self_test(size_t *completed_tests);
void traitfs_initialize(void);
enum traitfs_status traitfs_mount(enum traitfs_volume volume);
enum traitfs_status traitfs_unmount(enum traitfs_volume volume);
enum traitfs_status traitfs_sync(enum traitfs_volume volume);
struct traitfs_drive_info traitfs_drive(enum traitfs_volume volume);
uint64_t traitfs_completion_count(enum traitfs_volume volume);
enum traitfs_status traitfs_open(
    enum traitfs_volume volume,
    const char *path,
    enum traitfs_access access,
    traitfs_handle *handle
);
enum traitfs_status traitfs_close(traitfs_handle handle);
enum traitfs_status traitfs_read(
    traitfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    size_t *read_bytes
);
enum traitfs_status traitfs_pread(
    traitfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    uint64_t offset,
    size_t *read_bytes
);
enum traitfs_status traitfs_write(
    traitfs_handle handle,
    const uint8_t *source,
    size_t source_bytes,
    size_t *written_bytes
);
enum traitfs_status traitfs_seek(
    traitfs_handle handle,
    int64_t offset,
    enum traitfs_seek_origin origin,
    uint64_t *position
);
enum traitfs_status traitfs_stat_path(
    enum traitfs_volume volume,
    const char *path,
    struct traitfs_stat *stat
);
enum traitfs_status traitfs_list(
    enum traitfs_volume volume,
    const char *path,
    struct traitfs_list_entry *entries,
    size_t capacity,
    size_t *entry_count
);
enum traitfs_status traitfs_directory_open(
    enum traitfs_volume volume,
    const char *path,
    traitfs_directory_handle *handle
);
enum traitfs_status traitfs_directory_read(
    traitfs_directory_handle handle,
    struct traitfs_list_entry *entry,
    bool *present
);
enum traitfs_status traitfs_directory_close(traitfs_directory_handle handle);
enum traitfs_status traitfs_create(enum traitfs_volume volume, const char *path);
enum traitfs_status traitfs_create_mode(enum traitfs_volume volume,
    const char *path, uint16_t mode);
enum traitfs_status traitfs_truncate(
    enum traitfs_volume volume,
    const char *path,
    uint64_t size
);
enum traitfs_status traitfs_mkdir(enum traitfs_volume volume, const char *path);
enum traitfs_status traitfs_rename(
    enum traitfs_volume volume,
    const char *source,
    const char *destination
);
enum traitfs_status traitfs_unlink(enum traitfs_volume volume, const char *path);
enum traitfs_status traitfs_rmdir(enum traitfs_volume volume, const char *path);
enum traitfs_status traitfs_link(
    enum traitfs_volume volume,
    const char *source,
    const char *destination
);
const char *traitfs_status_string(enum traitfs_status status);

#endif
