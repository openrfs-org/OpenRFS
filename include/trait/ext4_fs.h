/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_EXT4_FS_H
#define TRAIT_EXT4_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <trait/fat32_fs.h>

#define TRAIT_EXT4_FILE_REGULAR 1U
#define TRAIT_EXT4_FILE_DIRECTORY 2U
#define TRAIT_EXT4_FILE_SYMLINK 3U

enum trait_ext4_status {
    TRAIT_EXT4_STATUS_OK = 0,
    TRAIT_EXT4_STATUS_NULL_ARGUMENT,
    TRAIT_EXT4_STATUS_VOLUME,
    TRAIT_EXT4_STATUS_IO,
    TRAIT_EXT4_STATUS_INVALID,
    TRAIT_EXT4_STATUS_NOT_FOUND,
    TRAIT_EXT4_STATUS_NOT_DIRECTORY,
    TRAIT_EXT4_STATUS_IS_DIRECTORY,
    TRAIT_EXT4_STATUS_RANGE,
    TRAIT_EXT4_STATUS_SPECIAL,
    TRAIT_EXT4_STATUS_EXISTS,
    TRAIT_EXT4_STATUS_NOT_EMPTY,
    TRAIT_EXT4_STATUS_COUNT
};

struct trait_ext4_metadata {
    uint64_t inode;
    uint64_t size;
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint16_t links;
    uint8_t file_type;
    uint8_t reserved[7];
};

struct trait_ext4_directory_entry {
    struct trait_ext4_metadata metadata;
    uint16_t name_length;
    uint8_t name[255];
    uint8_t reserved;
};

struct trait_ext4_identity {
    uint8_t label[16];
    uint8_t uuid[16];
    uint32_t recovered_transactions;
    uint32_t replayed_blocks;
    uint32_t consumed_slots;
    uint8_t recovery_performed;
    uint8_t reserved[3];
};

struct trait_ext4_recovery_report {
    uint32_t transactions;
    uint32_t replayed_blocks;
    uint32_t consumed_slots;
    bool performed;
};

struct trait_ext4_mount_diagnostic {
    enum traitfs_status begin_status;
    int32_t rust_status;
    enum traitfs_status close_status;
    int32_t nvme_close_status;
    int32_t nvme_teardown_status;
    uint32_t nvme_resource_mismatches;
};

enum trait_ext4_flush_boundary {
    TRAIT_EXT4_FLUSH_FILESYSTEM_STATE = 0,
    TRAIT_EXT4_FLUSH_ORDERED_DATA,
    TRAIT_EXT4_FLUSH_JOURNAL_PAYLOAD,
    TRAIT_EXT4_FLUSH_COMMIT,
    TRAIT_EXT4_FLUSH_CHECKPOINT,
    TRAIT_EXT4_FLUSH_JOURNAL_STATE,
    TRAIT_EXT4_FLUSH_COUNT
};

enum trait_ext4_test_storage_kind {
    TRAIT_EXT4_TEST_STORAGE_WRITE = 0,
    TRAIT_EXT4_TEST_STORAGE_FLUSH,
    TRAIT_EXT4_TEST_STORAGE_KIND_COUNT
};

/* Private Rust/C storage callbacks; valid only during a backend operation. */
int32_t trait_ext4_block_read(
    uintptr_t context,
    uint64_t start_byte,
    uint8_t *destination,
    size_t length
);
int32_t trait_ext4_block_write(
    uintptr_t context,
    uint64_t start_byte,
    const uint8_t *source,
    size_t length
);
int32_t trait_ext4_block_flush(uintptr_t context, uint32_t boundary);

void ext4_backend_initialize(void);
enum traitfs_status ext4_backend_mount(enum traitfs_volume volume);
enum traitfs_status ext4_backend_last_mount_status(enum traitfs_volume volume);
bool ext4_backend_mount_diagnostic(enum traitfs_volume volume,
    struct trait_ext4_mount_diagnostic *diagnostic);
enum traitfs_status ext4_backend_unmount(enum traitfs_volume volume);
enum traitfs_status ext4_backend_sync(enum traitfs_volume volume);
struct traitfs_drive_info ext4_backend_drive(enum traitfs_volume volume);
uint64_t ext4_backend_completion_count(enum traitfs_volume volume);
bool ext4_backend_recovery_report(enum traitfs_volume volume,
    struct trait_ext4_recovery_report *report);
enum traitfs_status ext4_backend_open(enum traitfs_volume volume,
    const char *path, enum traitfs_access access, traitfs_handle *handle);
enum traitfs_status ext4_backend_close(traitfs_handle handle);
enum traitfs_status ext4_backend_read(traitfs_handle handle,
    uint8_t *destination, size_t capacity, size_t *read_bytes);
enum traitfs_status ext4_backend_pread(traitfs_handle handle,
    uint8_t *destination, size_t capacity, uint64_t offset,
    size_t *read_bytes);
/* Shared journaled mutation entry points used by the backend and its probes. */
enum traitfs_status ext4_backend_transaction_probe(enum traitfs_volume volume,
    const char *path, uint64_t offset, const uint8_t *source,
    size_t source_bytes, size_t *written_bytes);
enum traitfs_status ext4_backend_truncate_probe(enum traitfs_volume volume,
    const char *path, uint64_t size);
enum traitfs_status ext4_backend_create_file_probe(enum traitfs_volume volume,
    const char *path, uint16_t mode);
enum traitfs_status ext4_backend_unlink_file_probe(enum traitfs_volume volume,
    const char *path);
enum traitfs_status ext4_backend_link_file_probe(enum traitfs_volume volume,
    const char *source, const char *destination);
enum traitfs_status ext4_backend_create_directory_probe(
    enum traitfs_volume volume, const char *path);
enum traitfs_status ext4_backend_remove_directory_probe(
    enum traitfs_volume volume, const char *path);
enum traitfs_status ext4_backend_rename_probe(enum traitfs_volume volume,
    const char *source, const char *destination);
/* Private ext4-recovery scenario controls; never installed in the VFS table. */
bool ext4_backend_test_configure_power_cut(const char *command_line,
    size_t command_line_length);
bool ext4_backend_test_power_cut_configured(void);
bool ext4_backend_test_fail_storage_once(uint32_t operation_ordinal);
bool ext4_backend_test_storage_failure_observed(
    enum trait_ext4_test_storage_kind expected_kind);
enum traitfs_status ext4_backend_write(traitfs_handle handle,
    const uint8_t *source, size_t source_bytes, size_t *written_bytes);
enum traitfs_status ext4_backend_seek(traitfs_handle handle, int64_t offset,
    enum traitfs_seek_origin origin, uint64_t *position);
enum traitfs_status ext4_backend_stat_path(enum traitfs_volume volume,
    const char *path, struct traitfs_stat *stat);
enum traitfs_status ext4_backend_list(enum traitfs_volume volume,
    const char *path, struct traitfs_list_entry *entries, size_t capacity,
    size_t *entry_count);
enum traitfs_status ext4_backend_directory_open(enum traitfs_volume volume,
    const char *path, traitfs_handle *handle);
enum traitfs_status ext4_backend_directory_read(traitfs_handle handle,
    struct traitfs_list_entry *entry, bool *present);
enum traitfs_status ext4_backend_directory_close(traitfs_handle handle);
enum traitfs_status ext4_backend_create(enum traitfs_volume volume,
    const char *path, uint16_t mode);
enum traitfs_status ext4_backend_truncate(enum traitfs_volume volume,
    const char *path, uint64_t size);
enum traitfs_status ext4_backend_mkdir(enum traitfs_volume volume,
    const char *path);
enum traitfs_status ext4_backend_rename(enum traitfs_volume volume,
    const char *source, const char *destination);
enum traitfs_status ext4_backend_unlink(enum traitfs_volume volume,
    const char *path);
enum traitfs_status ext4_backend_rmdir(enum traitfs_volume volume,
    const char *path);
enum traitfs_status ext4_backend_link(enum traitfs_volume volume,
    const char *source, const char *destination);

#endif
