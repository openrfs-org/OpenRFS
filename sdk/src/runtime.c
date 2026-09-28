/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/runtime.h>

#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define RSD_AUX_NULL UINT64_C(0)
#define RSD_AUX_TLS_IMAGE UINT64_C(0x53500002)
#define RSD_AUX_TLS_SIZE UINT64_C(0x53500003)
#define RSD_AUX_TLS_ALIGN UINT64_C(0x53500004)
#ifndef RSD_HOSTED
#define RSD_ATEXIT_MAX 16U
#endif

_Thread_local int errno;
static struct rsd_startup startup;
#ifndef RSD_HOSTED
static void (*exit_functions[RSD_ATEXIT_MAX])(void);
static size_t exit_function_count;
static volatile uint32_t exit_lock;
#endif

void rsd_runtime_initialize(int argc, char **argv, char **environment)
{
    char **cursor = environment;
    const uint64_t *auxiliary;

    startup.argc = argc;
    startup.argv = argv;
    startup.environment = environment;
    while (*cursor != NULL) {
        ++cursor;
    }
    auxiliary = (const uint64_t *)(const void *)(cursor + 1);
    while (auxiliary[0] != RSD_AUX_NULL) {
        if (auxiliary[0] == RSD_AUX_TLS_IMAGE) {
            startup.tls_image = auxiliary[1];
        } else if (auxiliary[0] == RSD_AUX_TLS_SIZE) {
            startup.tls_size = auxiliary[1];
        } else if (auxiliary[0] == RSD_AUX_TLS_ALIGN) {
            startup.tls_alignment = auxiliary[1];
        }
        auxiliary += 2;
    }
}

const struct rsd_startup *rsd_startup_information(void)
{
    return &startup;
}

int rsd_result(long result)
{
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

long rsd_handle_close(rsd_handle_t handle)
{
    return rsd_syscall1(RSD_SYS_HANDLE_CLOSE, handle);
}

long rsd_handle_duplicate(rsd_handle_t handle)
{
    return rsd_syscall1(RSD_SYS_HANDLE_DUPLICATE, handle);
}

long rsd_console_read(void *buffer, size_t length)
{
    return rsd_syscall2(RSD_SYS_CONSOLE_READ,
        (uint64_t)(uintptr_t)buffer, length);
}

long rsd_memory_allocate(
    size_t length,
    uint32_t flags,
    struct rsd_memory_map_response *response
)
{
    const struct rsd_memory_map_request request = {
        sizeof(request), RSD_ABI_VERSION, length, 0U, flags, 0U
    };

    return rsd_syscall2(RSD_SYS_MEMORY_MAP,
        (uint64_t)(uintptr_t)&request, (uint64_t)(uintptr_t)response);
}

long rsd_memory_release(uint64_t address, uint64_t length)
{
    return rsd_syscall2(RSD_SYS_MEMORY_UNMAP, address, length);
}

static long file_open_request(uint16_t volume, const char *path, uint32_t flags, uint16_t mode)
{
    struct rsd_file_open_request request;

    if (path == NULL) {
        return -RSD_EFAULT;
    }
    request.size = sizeof(request);
    request.version = RSD_ABI_VERSION;
    request.path.address = (uint64_t)(uintptr_t)path;
    request.path.length = (uint32_t)strlen(path);
    request.path.volume = volume;
    request.path.reserved = 0U;
    request.flags = flags;
    request.reserved = mode;
    return rsd_syscall1(RSD_SYS_FILE_OPEN,
        (uint64_t)(uintptr_t)&request);
}

long rsd_file_open(uint16_t volume, const char *path, uint32_t flags)
{
    return file_open_request(volume, path, flags, 0U);
}

long rsd_file_open_mode(uint16_t volume, const char *path, uint32_t flags, uint16_t mode)
{
    if ((flags & RSD_OPEN_CREATE) == 0U || (mode & ~07777U) != 0U) return -RSD_EINVAL;
    return file_open_request(volume, path, flags | RSD_OPEN_MODE_PRESENT, mode);
}

static long file_io(
    uint64_t number,
    rsd_handle_t handle,
    void *buffer,
    size_t length
)
{
    const struct rsd_io_request request = {
        sizeof(request), RSD_ABI_VERSION, handle,
        (uint64_t)(uintptr_t)buffer, UINT64_MAX, (uint32_t)length, 0U
    };

    if (length > UINT32_MAX) {
        return -RSD_EINVAL;
    }
    return rsd_syscall1(number, (uint64_t)(uintptr_t)&request);
}

long rsd_file_read(rsd_handle_t handle, void *buffer, size_t length)
{
    return file_io(RSD_SYS_FILE_READ, handle, buffer, length);
}

long rsd_file_write(
    rsd_handle_t handle,
    const void *buffer,
    size_t length
)
{
    return file_io(RSD_SYS_FILE_WRITE, handle,
        (void *)(uintptr_t)buffer, length);
}

long rsd_file_seek(rsd_handle_t handle, int64_t offset, uint32_t origin)
{
    const struct rsd_seek_request request = {
        sizeof(request), RSD_ABI_VERSION, handle, offset, origin, 0U
    };

    return rsd_syscall1(RSD_SYS_FILE_SEEK,
        (uint64_t)(uintptr_t)&request);
}

static struct rsd_path make_path(uint16_t volume, const char *path)
{
    const struct rsd_path result = {
        (uint64_t)(uintptr_t)path, (uint32_t)strlen(path), volume, 0U
    };

    return result;
}

static long single_path(uint64_t number, uint16_t volume, const char *path,
    uint64_t value)
{
    struct rsd_path input;

    if (path == NULL) {
        return -RSD_EFAULT;
    }
    input = make_path(volume, path);
    return rsd_syscall2(number, (uint64_t)(uintptr_t)&input, value);
}

long rsd_path_stat(
    uint16_t volume,
    const char *path,
    struct rsd_path_stat *result
)
{
    struct rsd_path input;

    if (path == NULL || result == NULL) {
        return -RSD_EFAULT;
    }
    input = make_path(volume, path);

    return rsd_syscall2(RSD_SYS_PATH_STAT,
        (uint64_t)(uintptr_t)&input, (uint64_t)(uintptr_t)result);
}

long rsd_path_metadata(uint16_t volume, const char *path, uint32_t flags,
    struct rsd_path_metadata *result)
{
    if (path == NULL || result == NULL) return -RSD_EFAULT;
    if ((flags & ~RSD_METADATA_NOFOLLOW) != 0U) return -RSD_EINVAL;
    const struct rsd_path input = make_path(volume, path);
    return rsd_syscall3(RSD_SYS_PATH_METADATA, (uint64_t)(uintptr_t)&input,
        (uint64_t)(uintptr_t)result, flags);
}

long rsd_directory_open(uint16_t volume, const char *path)
{
    struct rsd_path input;

    if (path == NULL) {
        return -RSD_EFAULT;
    }
    input = make_path(volume, path);

    return rsd_syscall1(RSD_SYS_DIRECTORY_OPEN,
        (uint64_t)(uintptr_t)&input);
}

long rsd_directory_read_long(rsd_handle_t handle, struct rsd_directory_entry_long *entry)
{
    return rsd_syscall2(RSD_SYS_DIRECTORY_READ_LONG, handle, (uint64_t)(uintptr_t)entry);
}

long rsd_directory_read(
    rsd_handle_t handle,
    struct rsd_directory_entry *entry
)
{
    return rsd_syscall2(RSD_SYS_DIRECTORY_READ, handle,
        (uint64_t)(uintptr_t)entry);
}

long rsd_path_mkdir(uint16_t volume, const char *path)
{
    return single_path(RSD_SYS_PATH_MKDIR, volume, path, 0U);
}

static long rename_path(uint64_t number, uint16_t volume, const char *source,
    const char *destination)
{
    struct rsd_rename_request request;

    if (source == NULL || destination == NULL) {
        return -RSD_EFAULT;
    }
    request.size = sizeof(request);
    request.version = RSD_ABI_VERSION;
    request.source = make_path(volume, source);
    request.destination = make_path(volume, destination);
    request.flags = 0U;
    request.reserved = 0U;
    return rsd_syscall1(number, (uint64_t)(uintptr_t)&request);
}

long rsd_path_rename(uint16_t volume, const char *source,
    const char *destination)
{
    return rename_path(RSD_SYS_PATH_RENAME, volume, source, destination);
}

long rsd_path_link(uint16_t volume, const char *source, const char *destination)
{
    return rename_path(RSD_SYS_PATH_LINK, volume, source, destination);
}

long rsd_path_replace(uint16_t volume, const char *source,
    const char *destination)
{
    return rename_path(RSD_SYS_PATH_REPLACE, volume, source, destination);
}

long rsd_path_unlink(uint16_t volume, const char *path)
{
    return single_path(RSD_SYS_PATH_UNLINK, volume, path, 0U);
}

long rsd_path_set_times(uint16_t volume, const char *path, const struct rsd_file_times *times)
{
    if (path == NULL || times == NULL) return -RSD_EFAULT;
    const struct rsd_set_times_request request = {
        sizeof(request), RSD_ABI_VERSION, make_path(volume, path), *times
    };
    return rsd_syscall1(RSD_SYS_PATH_SET_TIMES, (uint64_t)(uintptr_t)&request);
}

long rsd_file_truncate(rsd_handle_t handle, uint64_t size)
{
    return rsd_syscall2(RSD_SYS_FILE_TRUNCATE, handle, size);
}

long rsd_file_sync(rsd_handle_t handle)
{
    return rsd_syscall1(RSD_SYS_FILE_SYNC, handle);
}

long rsd_file_metadata(rsd_handle_t handle, struct rsd_path_metadata *result)
{
    if (result == NULL) return -RSD_EFAULT;
    return rsd_syscall2(RSD_SYS_FILE_METADATA, handle, (uint64_t)(uintptr_t)result);
}

long rsd_file_publish(rsd_handle_t handle, uint16_t volume,
    const char *source, const char *destination)
{
    if (source == NULL || destination == NULL) return -RSD_EFAULT;
    const struct rsd_rename_request request = {
        sizeof(request), RSD_ABI_VERSION, make_path(volume, source),
        make_path(volume, destination), 0U, 0U
    };
    return rsd_syscall2(RSD_SYS_FILE_PUBLISH, handle, (uint64_t)(uintptr_t)&request);
}

long rsd_file_unlink(rsd_handle_t handle, uint16_t volume, const char *path)
{
    if (path == NULL) return -RSD_EFAULT;
    const struct rsd_path request = make_path(volume, path);
    return rsd_syscall2(RSD_SYS_FILE_UNLINK, handle, (uint64_t)(uintptr_t)&request);
}

long rsd_path_chmod(uint16_t volume, const char *path, uint16_t mode)
{
    return single_path(RSD_SYS_PATH_CHMOD, volume, path, mode);
}

static long path_xattr(uint16_t volume, const char *path, const char *name,
    uint64_t value, size_t length, uint32_t operation)
{
    if (path == NULL || name == NULL || (length != 0U && value == 0U)) return -RSD_EFAULT;
    const size_t name_length = strlen(name);
    if (name_length == 0U || name_length > 255U || length > 4096U) return -RSD_EINVAL;
    struct rsd_xattr_request request = {
        sizeof(request), RSD_ABI_VERSION, make_path(volume, path),
        (uint64_t)(uintptr_t)name, (uint32_t)name_length, operation,
        value, (uint32_t)length, 0U
    };
    return rsd_syscall1(RSD_SYS_PATH_XATTR, (uint64_t)(uintptr_t)&request);
}

long rsd_path_set_xattr(uint16_t volume, const char *path, const char *name,
    const void *value, size_t length)
{
    return path_xattr(volume, path, name, (uint64_t)(uintptr_t)value, length, RSD_XATTR_SET);
}

long rsd_path_remove_xattr(uint16_t volume, const char *path, const char *name)
{
    return path_xattr(volume, path, name, 0U, 0U, RSD_XATTR_REMOVE);
}

long rsd_path_get_xattr(uint16_t volume, const char *path, const char *name,
    void *output, size_t capacity)
{
    return path_xattr(volume, path, name, (uint64_t)(uintptr_t)output, capacity, RSD_XATTR_GET);
}

long rsd_path_symlink(uint16_t volume, const char *path, const char *target)
{
    if (path == NULL || target == NULL) return -RSD_EFAULT;
    struct rsd_path request = make_path(volume, path);
    return rsd_syscall3(RSD_SYS_PATH_SYMLINK, (uint64_t)(uintptr_t)&request,
        (uint64_t)(uintptr_t)target, strlen(target));
}

long rsd_path_readlink(uint16_t volume, const char *path, void *output, size_t capacity)
{
    if (path == NULL || output == NULL) return -RSD_EFAULT;
    struct rsd_path request = make_path(volume, path);
    return rsd_syscall3(RSD_SYS_PATH_READLINK, (uint64_t)(uintptr_t)&request,
        (uint64_t)(uintptr_t)output, capacity);
}

long rsd_path_truncate(uint16_t volume, const char *path, uint64_t length)
{
    return single_path(RSD_SYS_PATH_TRUNCATE, volume, path, length);
}

long rsd_volume_sync(uint16_t volume)
{
    return rsd_syscall1(RSD_SYS_VOLUME_SYNC, volume);
}

long rsd_volume_space(uint16_t volume, struct rsd_volume_space *space)
{
    if (space == NULL) {
        return -RSD_EFAULT;
    }
    return rsd_syscall2(RSD_SYS_VOLUME_SPACE, volume,
        (uint64_t)(uintptr_t)space);
}

uint64_t rsd_monotonic_ns(void)
{
    const long result = rsd_syscall0(RSD_SYS_TIME_MONOTONIC);

    if (result < 0) {
        errno = (int)-result;
        return 0U;
    }
    return (uint64_t)result;
}

long rsd_realtime_seconds(void)
{
    return rsd_syscall0(RSD_SYS_TIME_REALTIME);
}

long rsd_sleep_until(uint64_t deadline_ns)
{
    return rsd_syscall1(RSD_SYS_SLEEP_UNTIL, deadline_ns);
}

long rsd_random(void *buffer, size_t length)
{
    return rsd_syscall2(RSD_SYS_RANDOM, (uint64_t)(uintptr_t)buffer,
        length);
}

long rsd_random_strong(void *buffer, size_t length)
{
    return rsd_syscall2(RSD_SYS_RANDOM_STRONG,
        (uint64_t)(uintptr_t)buffer, length);
}

int rsd_runtime_path(const char *input, struct rsd_runtime_path *result)
{
    if (input == NULL || result == NULL || *input == '\0') {
        errno = EINVAL;
        return -1;
    }
    result->volume = RSD_VOLUME_DATA;
    result->text = input;
    if (strncmp(input, "System:", 7U) == 0) {
        result->volume = RSD_VOLUME_SYSTEM;
        result->text += 7;
    } else if (strncmp(input, "Data:", 5U) == 0) {
        result->text += 5;
    }
    while (*result->text == '/') {
        ++result->text;
    }
    result->length = strlen(result->text);
    if (result->length == 0U || result->length > RSD_PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

void rsd_runtime_lock(volatile uint32_t *lock)
{
    while (__atomic_exchange_n(lock, 1U, __ATOMIC_ACQUIRE) != 0U) {
        const struct rsd_futex_request request = {
            sizeof(request), RSD_ABI_VERSION,
            (uint64_t)(uintptr_t)lock, 0U, 1U, 0U
        };
        (void)rsd_syscall1(RSD_SYS_FUTEX_WAIT,
            (uint64_t)(uintptr_t)&request);
    }
}

void rsd_runtime_unlock(volatile uint32_t *lock)
{
    const struct rsd_futex_request request = {
        sizeof(request), RSD_ABI_VERSION, (uint64_t)(uintptr_t)lock,
        0U, 0U, 1U
    };

    __atomic_store_n(lock, 0U, __ATOMIC_RELEASE);
    (void)rsd_syscall1(RSD_SYS_FUTEX_WAKE,
        (uint64_t)(uintptr_t)&request);
}

#ifndef RSD_HOSTED
int atexit(void (*function)(void))
{
    if (function == NULL) {
        errno = EINVAL;
        return -1;
    }
    rsd_runtime_lock(&exit_lock);
    if (exit_function_count == RSD_ATEXIT_MAX) {
        rsd_runtime_unlock(&exit_lock);
        errno = ENOMEM;
        return -1;
    }
    exit_functions[exit_function_count++] = function;
    rsd_runtime_unlock(&exit_lock);
    return 0;
}

_Noreturn void exit(int status)
{
    (void)fflush(NULL);
    while (exit_function_count != 0U) {
        exit_functions[--exit_function_count]();
    }
    (void)rsd_syscall1(RSD_SYS_EXIT, (uint64_t)(int64_t)status);
    __builtin_unreachable();
}

_Noreturn void abort(void)
{
    static const char message[] = "abort\n";

    (void)rsd_syscall2(RSD_SYS_CONSOLE_WRITE,
        (uint64_t)(uintptr_t)message, sizeof(message) - 1U);
    exit(134);
}
#endif

char *getenv(const char *name)
{
    const size_t length = name == NULL ? 0U : strlen(name);

    if (length == 0U) {
        return NULL;
    }
    for (char **entry = startup.environment; entry != NULL && *entry != NULL;
         ++entry) {
        if (strncmp(*entry, name, length) == 0 && (*entry)[length] == '=') {
            return *entry + length + 1U;
        }
    }
    return NULL;
}

void __rsd_assert(const char *expression, const char *file, int line)
{
    fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line,
        expression);
    abort();
}
