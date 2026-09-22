/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_EXT4_FS_H
#define OPENRFS_EXT4_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/fat32_fs.h>

#define OPENRFS_EXT4_FILE_REGULAR 1U
#define OPENRFS_EXT4_FILE_DIRECTORY 2U
#define OPENRFS_EXT4_FILE_SYMLINK 3U

/* Match the coordinator's bounded split-orphan reclaim profile. */
#define OPENRFS_EXT4_MAX_MUTABLE_FILE_BYTES UINT64_C(67108864)

enum openrfs_ext4_status {
    OPENRFS_EXT4_STATUS_OK = 0,
    OPENRFS_EXT4_STATUS_NULL_ARGUMENT,
    OPENRFS_EXT4_STATUS_VOLUME,
    OPENRFS_EXT4_STATUS_IO,
    OPENRFS_EXT4_STATUS_INVALID,
    OPENRFS_EXT4_STATUS_NOT_FOUND,
    OPENRFS_EXT4_STATUS_NOT_DIRECTORY,
    OPENRFS_EXT4_STATUS_IS_DIRECTORY,
    OPENRFS_EXT4_STATUS_RANGE,
    OPENRFS_EXT4_STATUS_SPECIAL,
    OPENRFS_EXT4_STATUS_EXISTS,
    OPENRFS_EXT4_STATUS_NOT_EMPTY,
    OPENRFS_EXT4_STATUS_FULL,
    OPENRFS_EXT4_STATUS_READ_ONLY,
    OPENRFS_EXT4_STATUS_BUSY,
    OPENRFS_EXT4_STATUS_NAME_TOO_LONG,
    OPENRFS_EXT4_STATUS_SYMLINK_LOOP,
    OPENRFS_EXT4_STATUS_STALE,
    OPENRFS_EXT4_STATUS_ARGUMENT,
    OPENRFS_EXT4_STATUS_COUNT
};

struct openrfs_ext4_metadata {
    uint64_t inode;
    uint64_t size;
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint16_t links;
    uint8_t file_type;
    uint8_t reserved[7];
    int64_t atime_seconds;
    int64_t mtime_seconds;
    int64_t ctime_seconds;
    uint32_t atime_nanos;
    uint32_t mtime_nanos;
    uint32_t ctime_nanos;
};

_Static_assert(sizeof(struct openrfs_ext4_metadata) == 80U, "ext4 metadata Rust ABI");
_Static_assert(offsetof(struct openrfs_ext4_metadata, atime_seconds) == 40U, "ext4 timestamp Rust ABI");

struct openrfs_ext4_directory_entry {
    struct openrfs_ext4_metadata metadata;
    uint16_t name_length;
    uint8_t name[255];
    uint8_t reserved;
};

struct openrfs_ext4_identity {
    uint8_t label[16];
    uint8_t uuid[16];
    uint32_t recovered_transactions;
    uint32_t replayed_blocks;
    uint32_t consumed_slots;
    uint8_t recovery_performed;
    uint8_t reserved[3];
};

struct openrfs_ext4_recovery_report {
    uint32_t transactions;
    uint32_t replayed_blocks;
    uint32_t consumed_slots;
    bool performed;
};

struct openrfs_ext4_mount_diagnostic {
    enum openrfsfs_status begin_status;
    int32_t rust_status;
    enum openrfsfs_status close_status;
    int32_t nvme_close_status;
    int32_t nvme_teardown_status;
    uint32_t nvme_resource_mismatches;
};

enum openrfs_ext4_flush_boundary {
    OPENRFS_EXT4_FLUSH_FILESYSTEM_STATE = 0,
    OPENRFS_EXT4_FLUSH_ORDERED_DATA,
    OPENRFS_EXT4_FLUSH_JOURNAL_PAYLOAD,
    OPENRFS_EXT4_FLUSH_COMMIT,
    OPENRFS_EXT4_FLUSH_CHECKPOINT,
    OPENRFS_EXT4_FLUSH_JOURNAL_STATE,
    OPENRFS_EXT4_FLUSH_COUNT
};

enum openrfs_ext4_test_storage_kind {
    OPENRFS_EXT4_TEST_STORAGE_WRITE = 0,
    OPENRFS_EXT4_TEST_STORAGE_FLUSH,
    OPENRFS_EXT4_TEST_STORAGE_KIND_COUNT
};

/* Private Rust/C storage callbacks; valid only during a backend operation. */
int32_t openrfs_ext4_block_read(
    uintptr_t context,
    uint64_t start_byte,
    uint8_t *destination,
    size_t length
);
int32_t openrfs_ext4_block_write(
    uintptr_t context,
    uint64_t start_byte,
    const uint8_t *source,
    size_t length
);
int32_t openrfs_ext4_block_flush(uintptr_t context, uint32_t boundary);
/* Inode-scoped retry/no-op durability boundary used by the C backend. */
int32_t openrfs_ext4_fsync(uintptr_t mounted, uint64_t inode);
uint64_t openrfs_ext4_current_time(uintptr_t context);

void ext4_backend_initialize(void);
/* Quiescent backend census; NVMe's allocation census is checked separately. */
bool ext4_backend_resources_released(void);
enum openrfsfs_status ext4_backend_mount(enum openrfsfs_volume volume);
enum openrfsfs_status ext4_backend_last_mount_status(enum openrfsfs_volume volume);
bool ext4_backend_mount_diagnostic(enum openrfsfs_volume volume,
    struct openrfs_ext4_mount_diagnostic *diagnostic);
enum openrfsfs_status ext4_backend_unmount(enum openrfsfs_volume volume);
enum openrfsfs_status ext4_backend_sync(enum openrfsfs_volume volume);
struct openrfsfs_drive_info ext4_backend_drive(enum openrfsfs_volume volume);
uint64_t ext4_backend_completion_count(enum openrfsfs_volume volume);
bool ext4_backend_recovery_report(enum openrfsfs_volume volume,
    struct openrfs_ext4_recovery_report *report);
enum openrfsfs_status ext4_backend_open(enum openrfsfs_volume volume,
    const char *path, enum openrfsfs_access access, openrfsfs_handle *handle);
enum openrfsfs_status ext4_backend_close(openrfsfs_handle handle);
enum openrfsfs_status ext4_backend_read(openrfsfs_handle handle,
    uint8_t *destination, size_t capacity, size_t *read_bytes);
enum openrfsfs_status ext4_backend_pread(openrfsfs_handle handle,
    uint8_t *destination, size_t capacity, uint64_t offset,
    size_t *read_bytes);
/* Shared journaled mutation entry points used by the backend and its probes. */
enum openrfsfs_status ext4_backend_transaction_probe(enum openrfsfs_volume volume,
    const char *path, uint64_t offset, const uint8_t *source,
    size_t source_bytes, size_t *written_bytes);
enum openrfsfs_status ext4_backend_truncate_probe(enum openrfsfs_volume volume,
    const char *path, uint64_t size);
enum openrfsfs_status ext4_backend_create_file_probe(enum openrfsfs_volume volume,
    const char *path, uint16_t mode);
enum openrfsfs_status ext4_backend_unlink_file_probe(enum openrfsfs_volume volume,
    const char *path);
enum openrfsfs_status ext4_backend_link_file_probe(enum openrfsfs_volume volume,
    const char *source, const char *destination);
enum openrfsfs_status ext4_backend_mkdir_mode(enum openrfsfs_volume volume, const char *path, uint16_t mode);
enum openrfsfs_status ext4_backend_create_directory_probe(
    enum openrfsfs_volume volume, const char *path);
enum openrfsfs_status ext4_backend_remove_directory_probe(
    enum openrfsfs_volume volume, const char *path);
enum openrfsfs_status ext4_backend_rename_probe(enum openrfsfs_volume volume,
    const char *source, const char *destination);
/* Private ext4-recovery scenario controls; never installed in the VFS table. */
bool ext4_backend_test_configure_power_cut(const char *command_line,
    size_t command_line_length);
bool ext4_backend_test_power_cut_configured(void);
bool ext4_backend_test_pause_storage_trace(bool paused);
bool ext4_backend_test_fail_storage_once(uint32_t operation_ordinal);
bool ext4_backend_test_finish_storage_probe(uint32_t *attempts,
    enum openrfs_ext4_test_storage_kind *failure_kind);
bool ext4_backend_test_storage_failure_observed(
    enum openrfs_ext4_test_storage_kind expected_kind);
enum openrfsfs_status ext4_backend_write(openrfsfs_handle handle,
    const uint8_t *source, size_t source_bytes, size_t *written_bytes);
enum openrfsfs_status ext4_backend_append(openrfsfs_handle handle,
    const uint8_t *source, size_t source_bytes, size_t *written_bytes);
enum openrfsfs_status ext4_backend_seek(openrfsfs_handle handle, int64_t offset,
    enum openrfsfs_seek_origin origin, uint64_t *position);
int32_t openrfs_ext4_prepare_open(uintptr_t mounted, const uint8_t *path, size_t path_length,
    uint8_t access, uint8_t flags, uint16_t mode, struct openrfs_ext4_metadata *metadata);
enum openrfsfs_status ext4_backend_open_options(enum openrfsfs_volume volume, const char *path,
    enum openrfsfs_access access, uint8_t flags, uint16_t mode,
    openrfsfs_handle *handle, struct openrfsfs_stat *stat);
enum openrfsfs_status ext4_backend_lstat_path(enum openrfsfs_volume volume, const char *path, struct openrfsfs_stat *stat);
enum openrfsfs_status ext4_backend_fsync(openrfsfs_handle handle);
enum openrfsfs_status ext4_backend_fstat(openrfsfs_handle handle, struct openrfsfs_stat *stat);
enum openrfsfs_status ext4_backend_publish_file(openrfsfs_handle handle, const char *source, const char *destination);
enum openrfsfs_status ext4_backend_unlink_held_file(openrfsfs_handle handle, const char *path);
int32_t openrfs_ext4_publish_file(uintptr_t mounted, const uint8_t *source, size_t source_length,
    const uint8_t *destination, size_t destination_length, uint64_t inode,
    const uint64_t *open_inodes, size_t open_count);
enum openrfsfs_status ext4_backend_stat_path(enum openrfsfs_volume volume,
    const char *path, struct openrfsfs_stat *stat);
enum openrfsfs_status ext4_backend_open_with_stat(enum openrfsfs_volume volume,
    const char *path, enum openrfsfs_access access, openrfsfs_handle *handle,
    struct openrfsfs_stat *stat);
enum openrfsfs_status ext4_backend_directory_open_with_stat(enum openrfsfs_volume volume,
    const char *path, openrfsfs_handle *handle, struct openrfsfs_stat *stat);
enum openrfsfs_status ext4_backend_list(enum openrfsfs_volume volume,
    const char *path, struct openrfsfs_list_entry *entries, size_t capacity,
    size_t *entry_count);
enum openrfsfs_status ext4_backend_directory_open(enum openrfsfs_volume volume,
    const char *path, openrfsfs_handle *handle);
enum openrfsfs_status ext4_backend_directory_read(openrfsfs_handle handle,
    struct openrfsfs_list_entry *entry, bool *present);
enum openrfsfs_status ext4_backend_directory_close(openrfsfs_handle handle);
enum openrfsfs_status ext4_backend_create(enum openrfsfs_volume volume,
    const char *path, uint16_t mode);
enum openrfsfs_status ext4_backend_truncate(enum openrfsfs_volume volume,
    const char *path, uint64_t size);
enum openrfsfs_status ext4_backend_mkdir(enum openrfsfs_volume volume,
    const char *path);
enum openrfsfs_status ext4_backend_rename(enum openrfsfs_volume volume,
    const char *source, const char *destination);
enum openrfsfs_status ext4_backend_unlink(enum openrfsfs_volume volume,
    const char *path);
enum openrfsfs_status ext4_backend_remove(enum openrfsfs_volume volume, const char *path);
enum openrfsfs_status ext4_backend_rmdir(enum openrfsfs_volume volume,
    const char *path);
enum openrfsfs_status ext4_backend_link(enum openrfsfs_volume volume,
    const char *source, const char *destination);
enum openrfsfs_status ext4_backend_symlink(enum openrfsfs_volume volume,
    const char *path, const char *target);
enum openrfsfs_status ext4_backend_rename_replace(enum openrfsfs_volume volume,
    const char *source, const char *destination);
enum openrfsfs_status ext4_backend_readlink(enum openrfsfs_volume volume,
    const char *path, uint8_t *output, size_t capacity, size_t *read_bytes);

enum openrfsfs_status ext4_backend_chmod(enum openrfsfs_volume volume,
    const char *path, uint16_t mode);
enum openrfsfs_status ext4_backend_ftruncate(openrfsfs_handle handle, uint64_t size);
enum openrfsfs_status ext4_backend_set_times(enum openrfsfs_volume volume, const char *path,
    const struct openrfsfs_times *times);
enum openrfsfs_status ext4_backend_set_xattr(enum openrfsfs_volume volume,
    const char *path, const char *name, const uint8_t *value, size_t length, bool remove);
enum openrfsfs_status ext4_backend_get_xattr(enum openrfsfs_volume volume,
    const char *path, const char *name, uint8_t *output, size_t capacity, size_t *length);

#endif
