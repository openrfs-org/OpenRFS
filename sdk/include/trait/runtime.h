/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_RUNTIME_H
#define TRAIT_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include <trait/abi.h>

#ifdef __cplusplus
extern "C" {
#endif

struct trait_startup {
    int argc;
    char **argv;
    char **environment;
    uint64_t tls_image;
    uint64_t tls_size;
    uint64_t tls_alignment;
};

long trait_syscall0(uint64_t number);
long trait_syscall1(uint64_t number, uint64_t argument0);
long trait_syscall2(uint64_t number, uint64_t argument0, uint64_t argument1);
long trait_syscall3(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2);
long trait_syscall4(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2, uint64_t argument3);
long trait_syscall5(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2, uint64_t argument3, uint64_t argument4);
long trait_syscall6(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2, uint64_t argument3, uint64_t argument4,
    uint64_t argument5);

void trait_runtime_initialize(int argc, char **argv, char **environment);
const struct trait_startup *trait_startup_information(void);
int trait_result(long result);
long trait_handle_close(trait_handle_t handle);
long trait_handle_duplicate(trait_handle_t handle);
long trait_console_read(void *buffer, size_t length);
long trait_memory_allocate(size_t length, uint32_t flags,
    struct trait_memory_map_response *response);
long trait_memory_release(uint64_t address, uint64_t length);
long trait_file_open(uint16_t volume, const char *path, uint32_t flags);
long trait_file_read(trait_handle_t handle, void *buffer, size_t length);
long trait_file_write(trait_handle_t handle, const void *buffer,
    size_t length);
long trait_file_seek(trait_handle_t handle, int64_t offset,
    uint32_t origin);
long trait_path_stat(uint16_t volume, const char *path,
    struct trait_path_stat *result);
long trait_directory_open(uint16_t volume, const char *path);
long trait_directory_read(trait_handle_t handle,
    struct trait_directory_entry *entry);
long trait_path_mkdir(uint16_t volume, const char *path);
long trait_path_rename(uint16_t volume, const char *source,
    const char *destination);
long trait_path_replace(uint16_t volume, const char *source,
    const char *destination);
long trait_path_unlink(uint16_t volume, const char *path);
long trait_path_truncate(uint16_t volume, const char *path, uint64_t length);
long trait_volume_sync(uint16_t volume);
long trait_volume_space(uint16_t volume, struct trait_volume_space *space);
uint64_t trait_monotonic_ns(void);
long trait_realtime_seconds(void);
long trait_sleep_until(uint64_t deadline_ns);
long trait_random(void *buffer, size_t length);
long trait_random_strong(void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif
