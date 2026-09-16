/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_RUNTIME_H
#define OPENGAT_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include <opengat/abi.h>

#ifdef __cplusplus
extern "C" {
#endif

struct opengat_startup {
    int argc;
    char **argv;
    char **environment;
    uint64_t tls_image;
    uint64_t tls_size;
    uint64_t tls_alignment;
};

long opengat_syscall0(uint64_t number);
long opengat_syscall1(uint64_t number, uint64_t argument0);
long opengat_syscall2(uint64_t number, uint64_t argument0, uint64_t argument1);
long opengat_syscall3(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2);
long opengat_syscall4(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2, uint64_t argument3);
long opengat_syscall5(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2, uint64_t argument3, uint64_t argument4);
long opengat_syscall6(uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2, uint64_t argument3, uint64_t argument4,
    uint64_t argument5);

void opengat_runtime_initialize(int argc, char **argv, char **environment);
const struct opengat_startup *opengat_startup_information(void);
int opengat_result(long result);
long opengat_handle_close(opengat_handle_t handle);
long opengat_handle_duplicate(opengat_handle_t handle);
long opengat_console_read(void *buffer, size_t length);
long opengat_memory_allocate(size_t length, uint32_t flags,
    struct opengat_memory_map_response *response);
long opengat_memory_release(uint64_t address, uint64_t length);
long opengat_file_open(uint16_t volume, const char *path, uint32_t flags);
long opengat_file_read(opengat_handle_t handle, void *buffer, size_t length);
long opengat_file_write(opengat_handle_t handle, const void *buffer,
    size_t length);
long opengat_file_seek(opengat_handle_t handle, int64_t offset,
    uint32_t origin);
long opengat_path_stat(uint16_t volume, const char *path,
    struct opengat_path_stat *result);
long opengat_directory_open(uint16_t volume, const char *path);
long opengat_directory_read(opengat_handle_t handle,
    struct opengat_directory_entry *entry);
long opengat_path_mkdir(uint16_t volume, const char *path);
long opengat_path_rename(uint16_t volume, const char *source,
    const char *destination);
long opengat_path_replace(uint16_t volume, const char *source,
    const char *destination);
long opengat_path_unlink(uint16_t volume, const char *path);
long opengat_path_truncate(uint16_t volume, const char *path, uint64_t length);
long opengat_volume_sync(uint16_t volume);
long opengat_volume_space(uint16_t volume, struct opengat_volume_space *space);
uint64_t opengat_monotonic_ns(void);
long opengat_realtime_seconds(void);
long opengat_sleep_until(uint64_t deadline_ns);
long opengat_random(void *buffer, size_t length);
long opengat_random_strong(void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif
