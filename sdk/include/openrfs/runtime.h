/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_RUNTIME_H
#define OPENRFS_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include <openrfs/abi.h>

#ifdef __cplusplus
extern "C" {
#endif

struct openrfs_startup {
    int argc;
    char **argv;
    char **environment;
    uint64_t tls_image;
    uint64_t tls_size;
    uint64_t tls_alignment;
};

long openrfs_syscall0(uint64_t number);
long openrfs_syscall1(uint64_t number, uint64_t argument0);
long openrfs_syscall2(uint64_t number, uint64_t argument0, uint64_t argument1);
long openrfs_syscall3(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2);
long openrfs_syscall4(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2, uint64_t argument3);
long openrfs_syscall5(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2, uint64_t argument3, uint64_t argument4);
long openrfs_syscall6(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2, uint64_t argument3, uint64_t argument4,
    uint64_t argument5);

void openrfs_runtime_initialize(int argc, char **argv, char **environment);
const struct openrfs_startup *openrfs_startup_information(void);
int openrfs_result(long result);
long openrfs_handle_close(openrfs_handle_t handle);
long openrfs_handle_duplicate(openrfs_handle_t handle);
long openrfs_console_read(void *buffer, size_t length);
long openrfs_memory_allocate(size_t length, uint32_t flags,
    struct openrfs_memory_map_response *response);
long openrfs_memory_release(uint64_t address, uint64_t length);
long openrfs_file_open(uint16_t volume, const char *path, uint32_t flags);
long openrfs_file_open_mode(uint16_t volume, const char *path, uint32_t flags, uint16_t mode);
long openrfs_file_read(openrfs_handle_t handle, void *buffer, size_t length);
long openrfs_file_write(openrfs_handle_t handle, const void *buffer,
    size_t length);
long openrfs_file_seek(openrfs_handle_t handle, int64_t offset,
    uint32_t origin);
long openrfs_path_stat(uint16_t volume, const char *path,
    struct openrfs_path_stat *result);
long openrfs_path_metadata(uint16_t volume, const char *path, uint32_t flags,
    struct openrfs_path_metadata *result);
long openrfs_directory_open(uint16_t volume, const char *path);
long openrfs_directory_read(openrfs_handle_t handle,
    struct openrfs_directory_entry *entry);
long openrfs_directory_read_long(openrfs_handle_t handle,
    struct openrfs_directory_entry_long *entry);
long openrfs_path_mkdir(uint16_t volume, const char *path);
long openrfs_path_rename(uint16_t volume, const char *source,
    const char *destination);
long openrfs_path_replace(uint16_t volume, const char *source,
    const char *destination);
long openrfs_path_unlink(uint16_t volume, const char *path);
long openrfs_path_symlink(uint16_t volume, const char *path, const char *target);
long openrfs_path_link(uint16_t volume, const char *source, const char *destination);
long openrfs_path_chmod(uint16_t volume, const char *path, uint16_t mode);
long openrfs_file_truncate(openrfs_handle_t handle, uint64_t size);
long openrfs_file_sync(openrfs_handle_t handle);
long openrfs_file_metadata(openrfs_handle_t handle, struct openrfs_path_metadata *result);
/* Data-only inode-bound publication/cleanup; unsupported backends refuse.
 * Synchronize the temporary file before publication. Errors can require sync
 * and exact retry; neither call closes the supplied handle. */
long openrfs_file_publish(openrfs_handle_t handle, uint16_t volume,
    const char *source, const char *destination);
long openrfs_file_unlink(openrfs_handle_t handle, uint16_t volume, const char *path);
long openrfs_path_set_times(uint16_t volume, const char *path, const struct openrfs_file_times *times);
long openrfs_path_set_xattr(uint16_t volume, const char *path, const char *name,
    const void *value, size_t length);
long openrfs_path_remove_xattr(uint16_t volume, const char *path, const char *name);
long openrfs_path_get_xattr(uint16_t volume, const char *path, const char *name,
    void *output, size_t capacity);
long openrfs_path_readlink(uint16_t volume, const char *path, void *output, size_t capacity);
long openrfs_path_truncate(uint16_t volume, const char *path, uint64_t length);
long openrfs_volume_sync(uint16_t volume);
long openrfs_volume_space(uint16_t volume, struct openrfs_volume_space *space);
uint64_t openrfs_monotonic_ns(void);
long openrfs_realtime_seconds(void);
long openrfs_sleep_until(uint64_t deadline_ns);
long openrfs_random(void *buffer, size_t length);
long openrfs_random_strong(void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif
