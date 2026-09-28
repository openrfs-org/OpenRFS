/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_RUNTIME_H
#define RSD_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include <rsd/abi.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rsd_startup {
    int argc;
    char **argv;
    char **environment;
    uint64_t tls_image;
    uint64_t tls_size;
    uint64_t tls_alignment;
};

long rsd_syscall0(uint64_t number);
long rsd_syscall1(uint64_t number, uint64_t argument0);
long rsd_syscall2(uint64_t number, uint64_t argument0, uint64_t argument1);
long rsd_syscall3(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2);
long rsd_syscall4(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2, uint64_t argument3);
long rsd_syscall5(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2, uint64_t argument3, uint64_t argument4);
long rsd_syscall6(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2, uint64_t argument3, uint64_t argument4,
    uint64_t argument5);

void rsd_runtime_initialize(int argc, char **argv, char **environment);
const struct rsd_startup *rsd_startup_information(void);
int rsd_result(long result);
long rsd_handle_close(rsd_handle_t handle);
long rsd_handle_duplicate(rsd_handle_t handle);
long rsd_console_read(void *buffer, size_t length);
long rsd_memory_allocate(size_t length, uint32_t flags,
    struct rsd_memory_map_response *response);
long rsd_memory_release(uint64_t address, uint64_t length);
long rsd_file_open(uint16_t volume, const char *path, uint32_t flags);
long rsd_file_open_mode(uint16_t volume, const char *path, uint32_t flags, uint16_t mode);
long rsd_file_read(rsd_handle_t handle, void *buffer, size_t length);
long rsd_file_write(rsd_handle_t handle, const void *buffer,
    size_t length);
long rsd_file_seek(rsd_handle_t handle, int64_t offset,
    uint32_t origin);
long rsd_path_stat(uint16_t volume, const char *path,
    struct rsd_path_stat *result);
long rsd_path_metadata(uint16_t volume, const char *path, uint32_t flags,
    struct rsd_path_metadata *result);
long rsd_directory_open(uint16_t volume, const char *path);
long rsd_directory_read(rsd_handle_t handle,
    struct rsd_directory_entry *entry);
long rsd_directory_read_long(rsd_handle_t handle,
    struct rsd_directory_entry_long *entry);
long rsd_path_mkdir(uint16_t volume, const char *path);
long rsd_path_rename(uint16_t volume, const char *source,
    const char *destination);
long rsd_path_replace(uint16_t volume, const char *source,
    const char *destination);
long rsd_path_unlink(uint16_t volume, const char *path);
long rsd_path_symlink(uint16_t volume, const char *path, const char *target);
long rsd_path_link(uint16_t volume, const char *source, const char *destination);
long rsd_path_chmod(uint16_t volume, const char *path, uint16_t mode);
long rsd_file_truncate(rsd_handle_t handle, uint64_t size);
long rsd_file_sync(rsd_handle_t handle);
long rsd_file_metadata(rsd_handle_t handle, struct rsd_path_metadata *result);
/* Data-only inode-bound publication/cleanup; unsupported backends refuse.
 * Synchronize the temporary file before publication. Errors can require sync
 * and exact retry; neither call closes the supplied handle. */
long rsd_file_publish(rsd_handle_t handle, uint16_t volume,
    const char *source, const char *destination);
long rsd_file_unlink(rsd_handle_t handle, uint16_t volume, const char *path);
long rsd_path_set_times(uint16_t volume, const char *path, const struct rsd_file_times *times);
long rsd_path_set_xattr(uint16_t volume, const char *path, const char *name,
    const void *value, size_t length);
long rsd_path_remove_xattr(uint16_t volume, const char *path, const char *name);
long rsd_path_get_xattr(uint16_t volume, const char *path, const char *name,
    void *output, size_t capacity);
long rsd_path_readlink(uint16_t volume, const char *path, void *output, size_t capacity);
long rsd_path_truncate(uint16_t volume, const char *path, uint64_t length);
long rsd_volume_sync(uint16_t volume);
long rsd_volume_space(uint16_t volume, struct rsd_volume_space *space);
uint64_t rsd_monotonic_ns(void);
long rsd_realtime_seconds(void);
long rsd_sleep_until(uint64_t deadline_ns);
long rsd_random(void *buffer, size_t length);
long rsd_random_strong(void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif
