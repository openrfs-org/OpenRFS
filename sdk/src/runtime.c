/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/runtime.h>

#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define TRAIT_AUX_NULL UINT64_C(0)
#define TRAIT_AUX_TLS_IMAGE UINT64_C(0x53500002)
#define TRAIT_AUX_TLS_SIZE UINT64_C(0x53500003)
#define TRAIT_AUX_TLS_ALIGN UINT64_C(0x53500004)
#define TRAIT_ATEXIT_MAX 16U

_Thread_local int errno;
static struct trait_startup startup;
static void (*exit_functions[TRAIT_ATEXIT_MAX])(void);
static size_t exit_function_count;
static volatile uint32_t exit_lock;

void trait_runtime_initialize(int argc, char **argv, char **environment)
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
    while (auxiliary[0] != TRAIT_AUX_NULL) {
        if (auxiliary[0] == TRAIT_AUX_TLS_IMAGE) {
            startup.tls_image = auxiliary[1];
        } else if (auxiliary[0] == TRAIT_AUX_TLS_SIZE) {
            startup.tls_size = auxiliary[1];
        } else if (auxiliary[0] == TRAIT_AUX_TLS_ALIGN) {
            startup.tls_alignment = auxiliary[1];
        }
        auxiliary += 2;
    }
}

const struct trait_startup *trait_startup_information(void)
{
    return &startup;
}

int trait_result(long result)
{
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

long trait_handle_close(trait_handle_t handle)
{
    return trait_syscall1(TRAIT_SYS_HANDLE_CLOSE, handle);
}

long trait_handle_duplicate(trait_handle_t handle)
{
    return trait_syscall1(TRAIT_SYS_HANDLE_DUPLICATE, handle);
}

long trait_console_read(void *buffer, size_t length)
{
    return trait_syscall2(TRAIT_SYS_CONSOLE_READ,
        (uint64_t)(uintptr_t)buffer, length);
}

long trait_memory_allocate(
    size_t length,
    uint32_t flags,
    struct trait_memory_map_response *response
)
{
    const struct trait_memory_map_request request = {
        sizeof(request), TRAIT_ABI_VERSION, length, 0U, flags, 0U
    };

    return trait_syscall2(TRAIT_SYS_MEMORY_MAP,
        (uint64_t)(uintptr_t)&request, (uint64_t)(uintptr_t)response);
}

long trait_memory_release(uint64_t address, uint64_t length)
{
    return trait_syscall2(TRAIT_SYS_MEMORY_UNMAP, address, length);
}

long trait_file_open(uint16_t volume, const char *path, uint32_t flags)
{
    struct trait_file_open_request request;

    if (path == NULL) {
        return -TRAIT_EFAULT;
    }
    request.size = sizeof(request);
    request.version = TRAIT_ABI_VERSION;
    request.path.address = (uint64_t)(uintptr_t)path;
    request.path.length = (uint32_t)strlen(path);
    request.path.volume = volume;
    request.path.reserved = 0U;
    request.flags = flags;
    request.reserved = 0U;
    return trait_syscall1(TRAIT_SYS_FILE_OPEN,
        (uint64_t)(uintptr_t)&request);
}

static long file_io(
    uint64_t number,
    trait_handle_t handle,
    void *buffer,
    size_t length
)
{
    const struct trait_io_request request = {
        sizeof(request), TRAIT_ABI_VERSION, handle,
        (uint64_t)(uintptr_t)buffer, UINT64_MAX, (uint32_t)length, 0U
    };

    if (length > UINT32_MAX) {
        return -TRAIT_EINVAL;
    }
    return trait_syscall1(number, (uint64_t)(uintptr_t)&request);
}

long trait_file_read(trait_handle_t handle, void *buffer, size_t length)
{
    return file_io(TRAIT_SYS_FILE_READ, handle, buffer, length);
}

long trait_file_write(
    trait_handle_t handle,
    const void *buffer,
    size_t length
)
{
    return file_io(TRAIT_SYS_FILE_WRITE, handle,
        (void *)(uintptr_t)buffer, length);
}

long trait_file_seek(trait_handle_t handle, int64_t offset, uint32_t origin)
{
    const struct trait_seek_request request = {
        sizeof(request), TRAIT_ABI_VERSION, handle, offset, origin, 0U
    };

    return trait_syscall1(TRAIT_SYS_FILE_SEEK,
        (uint64_t)(uintptr_t)&request);
}

static struct trait_path make_path(uint16_t volume, const char *path)
{
    const struct trait_path result = {
        (uint64_t)(uintptr_t)path, (uint32_t)strlen(path), volume, 0U
    };

    return result;
}

static long single_path(uint64_t number, uint16_t volume, const char *path,
    uint64_t value)
{
    struct trait_path input;

    if (path == NULL) {
        return -TRAIT_EFAULT;
    }
    input = make_path(volume, path);
    return trait_syscall2(number, (uint64_t)(uintptr_t)&input, value);
}

long trait_path_stat(
    uint16_t volume,
    const char *path,
    struct trait_path_stat *result
)
{
    struct trait_path input;

    if (path == NULL || result == NULL) {
        return -TRAIT_EFAULT;
    }
    input = make_path(volume, path);

    return trait_syscall2(TRAIT_SYS_PATH_STAT,
        (uint64_t)(uintptr_t)&input, (uint64_t)(uintptr_t)result);
}

long trait_directory_open(uint16_t volume, const char *path)
{
    struct trait_path input;

    if (path == NULL) {
        return -TRAIT_EFAULT;
    }
    input = make_path(volume, path);

    return trait_syscall1(TRAIT_SYS_DIRECTORY_OPEN,
        (uint64_t)(uintptr_t)&input);
}

long trait_directory_read(
    trait_handle_t handle,
    struct trait_directory_entry *entry
)
{
    return trait_syscall2(TRAIT_SYS_DIRECTORY_READ, handle,
        (uint64_t)(uintptr_t)entry);
}

long trait_path_mkdir(uint16_t volume, const char *path)
{
    return single_path(TRAIT_SYS_PATH_MKDIR, volume, path, 0U);
}

static long rename_path(uint64_t number, uint16_t volume, const char *source,
    const char *destination)
{
    struct trait_rename_request request;

    if (source == NULL || destination == NULL) {
        return -TRAIT_EFAULT;
    }
    request.size = sizeof(request);
    request.version = TRAIT_ABI_VERSION;
    request.source = make_path(volume, source);
    request.destination = make_path(volume, destination);
    request.flags = 0U;
    request.reserved = 0U;
    return trait_syscall1(number, (uint64_t)(uintptr_t)&request);
}

long trait_path_rename(uint16_t volume, const char *source,
    const char *destination)
{
    return rename_path(TRAIT_SYS_PATH_RENAME, volume, source, destination);
}

long trait_path_replace(uint16_t volume, const char *source,
    const char *destination)
{
    return rename_path(TRAIT_SYS_PATH_REPLACE, volume, source, destination);
}

long trait_path_unlink(uint16_t volume, const char *path)
{
    return single_path(TRAIT_SYS_PATH_UNLINK, volume, path, 0U);
}

long trait_path_truncate(uint16_t volume, const char *path, uint64_t length)
{
    return single_path(TRAIT_SYS_PATH_TRUNCATE, volume, path, length);
}

long trait_volume_sync(uint16_t volume)
{
    return trait_syscall1(TRAIT_SYS_VOLUME_SYNC, volume);
}

long trait_volume_space(uint16_t volume, struct trait_volume_space *space)
{
    if (space == NULL) {
        return -TRAIT_EFAULT;
    }
    return trait_syscall2(TRAIT_SYS_VOLUME_SPACE, volume,
        (uint64_t)(uintptr_t)space);
}

uint64_t trait_monotonic_ns(void)
{
    const long result = trait_syscall0(TRAIT_SYS_TIME_MONOTONIC);

    if (result < 0) {
        errno = (int)-result;
        return 0U;
    }
    return (uint64_t)result;
}

long trait_realtime_seconds(void)
{
    return trait_syscall0(TRAIT_SYS_TIME_REALTIME);
}

long trait_sleep_until(uint64_t deadline_ns)
{
    return trait_syscall1(TRAIT_SYS_SLEEP_UNTIL, deadline_ns);
}

long trait_random(void *buffer, size_t length)
{
    return trait_syscall2(TRAIT_SYS_RANDOM, (uint64_t)(uintptr_t)buffer,
        length);
}

long trait_random_strong(void *buffer, size_t length)
{
    return trait_syscall2(TRAIT_SYS_RANDOM_STRONG,
        (uint64_t)(uintptr_t)buffer, length);
}

int trait_runtime_path(const char *input, struct trait_runtime_path *result)
{
    if (input == NULL || result == NULL || *input == '\0') {
        errno = EINVAL;
        return -1;
    }
    result->volume = TRAIT_VOLUME_DATA;
    result->text = input;
    if (strncmp(input, "System:", 7U) == 0) {
        result->volume = TRAIT_VOLUME_SYSTEM;
        result->text += 7;
    } else if (strncmp(input, "Data:", 5U) == 0) {
        result->text += 5;
    }
    while (*result->text == '/') {
        ++result->text;
    }
    result->length = strlen(result->text);
    if (result->length == 0U || result->length > TRAIT_PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

void trait_runtime_lock(volatile uint32_t *lock)
{
    while (__atomic_exchange_n(lock, 1U, __ATOMIC_ACQUIRE) != 0U) {
        const struct trait_futex_request request = {
            sizeof(request), TRAIT_ABI_VERSION,
            (uint64_t)(uintptr_t)lock, 0U, 1U, 0U
        };
        (void)trait_syscall1(TRAIT_SYS_FUTEX_WAIT,
            (uint64_t)(uintptr_t)&request);
    }
}

void trait_runtime_unlock(volatile uint32_t *lock)
{
    const struct trait_futex_request request = {
        sizeof(request), TRAIT_ABI_VERSION, (uint64_t)(uintptr_t)lock,
        0U, 0U, 1U
    };

    __atomic_store_n(lock, 0U, __ATOMIC_RELEASE);
    (void)trait_syscall1(TRAIT_SYS_FUTEX_WAKE,
        (uint64_t)(uintptr_t)&request);
}

int atexit(void (*function)(void))
{
    if (function == NULL) {
        errno = EINVAL;
        return -1;
    }
    trait_runtime_lock(&exit_lock);
    if (exit_function_count == TRAIT_ATEXIT_MAX) {
        trait_runtime_unlock(&exit_lock);
        errno = ENOMEM;
        return -1;
    }
    exit_functions[exit_function_count++] = function;
    trait_runtime_unlock(&exit_lock);
    return 0;
}

_Noreturn void exit(int status)
{
    (void)fflush(NULL);
    while (exit_function_count != 0U) {
        exit_functions[--exit_function_count]();
    }
    (void)trait_syscall1(TRAIT_SYS_EXIT, (uint64_t)(int64_t)status);
    __builtin_unreachable();
}

_Noreturn void abort(void)
{
    static const char message[] = "abort\n";

    (void)trait_syscall2(TRAIT_SYS_CONSOLE_WRITE,
        (uint64_t)(uintptr_t)message, sizeof(message) - 1U);
    exit(134);
}

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

void __trait_assert(const char *expression, const char *file, int line)
{
    fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line,
        expression);
    abort();
}
