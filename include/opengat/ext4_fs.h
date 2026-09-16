/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_EXT4_FS_H
#define OPENGAT_EXT4_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <opengat/fat32_fs.h>

#define OPENGAT_EXT4_FILE_REGULAR 1U
#define OPENGAT_EXT4_FILE_DIRECTORY 2U
#define OPENGAT_EXT4_FILE_SYMLINK 3U

enum opengat_ext4_status {
    OPENGAT_EXT4_STATUS_OK = 0,
    OPENGAT_EXT4_STATUS_NULL_ARGUMENT,
    OPENGAT_EXT4_STATUS_VOLUME,
    OPENGAT_EXT4_STATUS_IO,
    OPENGAT_EXT4_STATUS_INVALID,
    OPENGAT_EXT4_STATUS_NOT_FOUND,
    OPENGAT_EXT4_STATUS_NOT_DIRECTORY,
    OPENGAT_EXT4_STATUS_IS_DIRECTORY,
    OPENGAT_EXT4_STATUS_RANGE,
    OPENGAT_EXT4_STATUS_SPECIAL,
    OPENGAT_EXT4_STATUS_EXISTS,
    OPENGAT_EXT4_STATUS_NOT_EMPTY,
    OPENGAT_EXT4_STATUS_COUNT
};

struct opengat_ext4_metadata {
    uint64_t inode;
    uint64_t size;
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint16_t links;
    uint8_t file_type;
    uint8_t reserved[7];
};

struct opengat_ext4_directory_entry {
    struct opengat_ext4_metadata metadata;
    uint16_t name_length;
    uint8_t name[255];
    uint8_t reserved;
};

struct opengat_ext4_identity {
    uint8_t label[16];
    uint8_t uuid[16];
    uint32_t recovered_transactions;
    uint32_t replayed_blocks;
    uint32_t consumed_slots;
    uint8_t recovery_performed;
    uint8_t reserved[3];
};

struct opengat_ext4_recovery_report {
    uint32_t transactions;
    uint32_t replayed_blocks;
    uint32_t consumed_slots;
    bool performed;
};

struct opengat_ext4_mount_diagnostic {
    enum opengatfs_status begin_status;
    int32_t rust_status;
    enum opengatfs_status close_status;
    int32_t nvme_close_status;
    int32_t nvme_teardown_status;
    uint32_t nvme_resource_mismatches;
};

enum opengat_ext4_flush_boundary {
    OPENGAT_EXT4_FLUSH_FILESYSTEM_STATE = 0,
    OPENGAT_EXT4_FLUSH_ORDERED_DATA,
    OPENGAT_EXT4_FLUSH_JOURNAL_PAYLOAD,
    OPENGAT_EXT4_FLUSH_COMMIT,
    OPENGAT_EXT4_FLUSH_CHECKPOINT,
    OPENGAT_EXT4_FLUSH_JOURNAL_STATE,
    OPENGAT_EXT4_FLUSH_COUNT
};

enum opengat_ext4_test_storage_kind {
    OPENGAT_EXT4_TEST_STORAGE_WRITE = 0,
    OPENGAT_EXT4_TEST_STORAGE_FLUSH,
    OPENGAT_EXT4_TEST_STORAGE_KIND_COUNT
};

/* Private Rust/C storage callbacks; valid only during a backend operation. */
int32_t opengat_ext4_block_read(
    uintptr_t context,
    uint64_t start_byte,
    uint8_t *destination,
    size_t length
);
int32_t opengat_ext4_block_write(
    uintptr_t context,
    uint64_t start_byte,
    const uint8_t *source,
    size_t length
);
int32_t opengat_ext4_block_flush(uintptr_t context, uint32_t boundary);

void ext4_backend_initialize(void);
enum opengatfs_status ext4_backend_mount(enum opengatfs_volume volume);
enum opengatfs_status ext4_backend_last_mount_status(enum opengatfs_volume volume);
bool ext4_backend_mount_diagnostic(enum opengatfs_volume volume,
    struct opengat_ext4_mount_diagnostic *diagnostic);
enum opengatfs_status ext4_backend_unmount(enum opengatfs_volume volume);
enum opengatfs_status ext4_backend_sync(enum opengatfs_volume volume);
struct opengatfs_drive_info ext4_backend_drive(enum opengatfs_volume volume);
uint64_t ext4_backend_completion_count(enum opengatfs_volume volume);
bool ext4_backend_recovery_report(enum opengatfs_volume volume,
    struct opengat_ext4_recovery_report *report);
enum opengatfs_status ext4_backend_open(enum opengatfs_volume volume,
    const char *path, enum opengatfs_access access, opengatfs_handle *handle);
enum opengatfs_status ext4_backend_close(opengatfs_handle handle);
enum opengatfs_status ext4_backend_read(opengatfs_handle handle,
    uint8_t *destination, size_t capacity, size_t *read_bytes);
enum opengatfs_status ext4_backend_pread(opengatfs_handle handle,
    uint8_t *destination, size_t capacity, uint64_t offset,
    size_t *read_bytes);
/* Shared journaled mutation entry points used by the backend and its probes. */
enum opengatfs_status ext4_backend_transaction_probe(enum opengatfs_volume volume,
    const char *path, uint64_t offset, const uint8_t *source,
    size_t source_bytes, size_t *written_bytes);
enum opengatfs_status ext4_backend_truncate_probe(enum opengatfs_volume volume,
    const char *path, uint64_t size);
enum opengatfs_status ext4_backend_create_file_probe(enum opengatfs_volume volume,
    const char *path, uint16_t mode);
enum opengatfs_status ext4_backend_unlink_file_probe(enum opengatfs_volume volume,
    const char *path);
enum opengatfs_status ext4_backend_link_file_probe(enum opengatfs_volume volume,
    const char *source, const char *destination);
enum opengatfs_status ext4_backend_create_directory_probe(
    enum opengatfs_volume volume, const char *path);
enum opengatfs_status ext4_backend_remove_directory_probe(
    enum opengatfs_volume volume, const char *path);
enum opengatfs_status ext4_backend_rename_probe(enum opengatfs_volume volume,
    const char *source, const char *destination);
/* Private ext4-recovery scenario controls; never installed in the VFS table. */
bool ext4_backend_test_configure_power_cut(const char *command_line,
    size_t command_line_length);
bool ext4_backend_test_power_cut_configured(void);
bool ext4_backend_test_fail_storage_once(uint32_t operation_ordinal);
bool ext4_backend_test_storage_failure_observed(
    enum opengat_ext4_test_storage_kind expected_kind);
enum opengatfs_status ext4_backend_write(opengatfs_handle handle,
    const uint8_t *source, size_t source_bytes, size_t *written_bytes);
enum opengatfs_status ext4_backend_seek(opengatfs_handle handle, int64_t offset,
    enum opengatfs_seek_origin origin, uint64_t *position);
enum opengatfs_status ext4_backend_stat_path(enum opengatfs_volume volume,
    const char *path, struct opengatfs_stat *stat);
enum opengatfs_status ext4_backend_list(enum opengatfs_volume volume,
    const char *path, struct opengatfs_list_entry *entries, size_t capacity,
    size_t *entry_count);
enum opengatfs_status ext4_backend_directory_open(enum opengatfs_volume volume,
    const char *path, opengatfs_handle *handle);
enum opengatfs_status ext4_backend_directory_read(opengatfs_handle handle,
    struct opengatfs_list_entry *entry, bool *present);
enum opengatfs_status ext4_backend_directory_close(opengatfs_handle handle);
enum opengatfs_status ext4_backend_create(enum opengatfs_volume volume,
    const char *path, uint16_t mode);
enum opengatfs_status ext4_backend_truncate(enum opengatfs_volume volume,
    const char *path, uint64_t size);
enum opengatfs_status ext4_backend_mkdir(enum opengatfs_volume volume,
    const char *path);
enum opengatfs_status ext4_backend_rename(enum opengatfs_volume volume,
    const char *source, const char *destination);
enum opengatfs_status ext4_backend_unlink(enum opengatfs_volume volume,
    const char *path);
enum opengatfs_status ext4_backend_rmdir(enum opengatfs_volume volume,
    const char *path);
enum opengatfs_status ext4_backend_link(enum opengatfs_volume volume,
    const char *source, const char *destination);

#endif
