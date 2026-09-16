/* SPDX-License-Identifier: GPL-3.0-only */
#include <opengat/runtime.h>

#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define OPENGAT_AUX_NULL UINT64_C(0)
#define OPENGAT_AUX_TLS_IMAGE UINT64_C(0x53500002)
#define OPENGAT_AUX_TLS_SIZE UINT64_C(0x53500003)
#define OPENGAT_AUX_TLS_ALIGN UINT64_C(0x53500004)
#define OPENGAT_ATEXIT_MAX 16U

_Thread_local int errno;
static struct opengat_startup startup;
static void (*exit_functions[OPENGAT_ATEXIT_MAX])(void);
static size_t exit_function_count;
static volatile uint32_t exit_lock;

void opengat_runtime_initialize(int argc, char **argv, char **environment)
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
    while (auxiliary[0] != OPENGAT_AUX_NULL) {
        if (auxiliary[0] == OPENGAT_AUX_TLS_IMAGE) {
            startup.tls_image = auxiliary[1];
        } else if (auxiliary[0] == OPENGAT_AUX_TLS_SIZE) {
            startup.tls_size = auxiliary[1];
        } else if (auxiliary[0] == OPENGAT_AUX_TLS_ALIGN) {
            startup.tls_alignment = auxiliary[1];
        }
        auxiliary += 2;
    }
}

const struct opengat_startup *opengat_startup_information(void)
{
    return &startup;
}

int opengat_result(long result)
{
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

long opengat_handle_close(opengat_handle_t handle)
{
    return opengat_syscall1(OPENGAT_SYS_HANDLE_CLOSE, handle);
}

long opengat_handle_duplicate(opengat_handle_t handle)
{
    return opengat_syscall1(OPENGAT_SYS_HANDLE_DUPLICATE, handle);
}

long opengat_console_read(void *buffer, size_t length)
{
    return opengat_syscall2(OPENGAT_SYS_CONSOLE_READ,
        (uint64_t)(uintptr_t)buffer, length);
}

long opengat_memory_allocate(
    size_t length,
    uint32_t flags,
    struct opengat_memory_map_response *response
)
{
    const struct opengat_memory_map_request request = {
        sizeof(request), OPENGAT_ABI_VERSION, length, 0U, flags, 0U
    };

    return opengat_syscall2(OPENGAT_SYS_MEMORY_MAP,
        (uint64_t)(uintptr_t)&request, (uint64_t)(uintptr_t)response);
}

long opengat_memory_release(uint64_t address, uint64_t length)
{
    return opengat_syscall2(OPENGAT_SYS_MEMORY_UNMAP, address, length);
}

long opengat_file_open(uint16_t volume, const char *path, uint32_t flags)
{
    struct opengat_file_open_request request;

    if (path == NULL) {
        return -OPENGAT_EFAULT;
    }
    request.size = sizeof(request);
    request.version = OPENGAT_ABI_VERSION;
    request.path.address = (uint64_t)(uintptr_t)path;
    request.path.length = (uint32_t)strlen(path);
    request.path.volume = volume;
    request.path.reserved = 0U;
    request.flags = flags;
    request.reserved = 0U;
    return opengat_syscall1(OPENGAT_SYS_FILE_OPEN,
        (uint64_t)(uintptr_t)&request);
}

static long file_io(
    uint64_t number,
    opengat_handle_t handle,
    void *buffer,
    size_t length
)
{
    const struct opengat_io_request request = {
        sizeof(request), OPENGAT_ABI_VERSION, handle,
        (uint64_t)(uintptr_t)buffer, UINT64_MAX, (uint32_t)length, 0U
    };

    if (length > UINT32_MAX) {
        return -OPENGAT_EINVAL;
    }
    return opengat_syscall1(number, (uint64_t)(uintptr_t)&request);
}

long opengat_file_read(opengat_handle_t handle, void *buffer, size_t length)
{
    return file_io(OPENGAT_SYS_FILE_READ, handle, buffer, length);
}

long opengat_file_write(
    opengat_handle_t handle,
    const void *buffer,
    size_t length
)
{
    return file_io(OPENGAT_SYS_FILE_WRITE, handle,
        (void *)(uintptr_t)buffer, length);
}

long opengat_file_seek(opengat_handle_t handle, int64_t offset, uint32_t origin)
{
    const struct opengat_seek_request request = {
        sizeof(request), OPENGAT_ABI_VERSION, handle, offset, origin, 0U
    };

    return opengat_syscall1(OPENGAT_SYS_FILE_SEEK,
        (uint64_t)(uintptr_t)&request);
}

static struct opengat_path make_path(uint16_t volume, const char *path)
{
    const struct opengat_path result = {
        (uint64_t)(uintptr_t)path, (uint32_t)strlen(path), volume, 0U
    };

    return result;
}

static long single_path(uint64_t number, uint16_t volume, const char *path,
    uint64_t value)
{
    struct opengat_path input;

    if (path == NULL) {
        return -OPENGAT_EFAULT;
    }
    input = make_path(volume, path);
    return opengat_syscall2(number, (uint64_t)(uintptr_t)&input, value);
}

long opengat_path_stat(
    uint16_t volume,
    const char *path,
    struct opengat_path_stat *result
)
{
    struct opengat_path input;

    if (path == NULL || result == NULL) {
        return -OPENGAT_EFAULT;
    }
    input = make_path(volume, path);

    return opengat_syscall2(OPENGAT_SYS_PATH_STAT,
        (uint64_t)(uintptr_t)&input, (uint64_t)(uintptr_t)result);
}

long opengat_directory_open(uint16_t volume, const char *path)
{
    struct opengat_path input;

    if (path == NULL) {
        return -OPENGAT_EFAULT;
    }
    input = make_path(volume, path);

    return opengat_syscall1(OPENGAT_SYS_DIRECTORY_OPEN,
        (uint64_t)(uintptr_t)&input);
}

long opengat_directory_read(
    opengat_handle_t handle,
    struct opengat_directory_entry *entry
)
{
    return opengat_syscall2(OPENGAT_SYS_DIRECTORY_READ, handle,
        (uint64_t)(uintptr_t)entry);
}

long opengat_path_mkdir(uint16_t volume, const char *path)
{
    return single_path(OPENGAT_SYS_PATH_MKDIR, volume, path, 0U);
}

static long rename_path(uint64_t number, uint16_t volume, const char *source,
    const char *destination)
{
    struct opengat_rename_request request;

    if (source == NULL || destination == NULL) {
        return -OPENGAT_EFAULT;
    }
    request.size = sizeof(request);
    request.version = OPENGAT_ABI_VERSION;
    request.source = make_path(volume, source);
    request.destination = make_path(volume, destination);
    request.flags = 0U;
    request.reserved = 0U;
    return opengat_syscall1(number, (uint64_t)(uintptr_t)&request);
}

long opengat_path_rename(uint16_t volume, const char *source,
    const char *destination)
{
    return rename_path(OPENGAT_SYS_PATH_RENAME, volume, source, destination);
}

long opengat_path_replace(uint16_t volume, const char *source,
    const char *destination)
{
    return rename_path(OPENGAT_SYS_PATH_REPLACE, volume, source, destination);
}

long opengat_path_unlink(uint16_t volume, const char *path)
{
    return single_path(OPENGAT_SYS_PATH_UNLINK, volume, path, 0U);
}

long opengat_path_truncate(uint16_t volume, const char *path, uint64_t length)
{
    return single_path(OPENGAT_SYS_PATH_TRUNCATE, volume, path, length);
}

long opengat_volume_sync(uint16_t volume)
{
    return opengat_syscall1(OPENGAT_SYS_VOLUME_SYNC, volume);
}

long opengat_volume_space(uint16_t volume, struct opengat_volume_space *space)
{
    if (space == NULL) {
        return -OPENGAT_EFAULT;
    }
    return opengat_syscall2(OPENGAT_SYS_VOLUME_SPACE, volume,
        (uint64_t)(uintptr_t)space);
}

uint64_t opengat_monotonic_ns(void)
{
    const long result = opengat_syscall0(OPENGAT_SYS_TIME_MONOTONIC);

    if (result < 0) {
        errno = (int)-result;
        return 0U;
    }
    return (uint64_t)result;
}

long opengat_realtime_seconds(void)
{
    return opengat_syscall0(OPENGAT_SYS_TIME_REALTIME);
}

long opengat_sleep_until(uint64_t deadline_ns)
{
    return opengat_syscall1(OPENGAT_SYS_SLEEP_UNTIL, deadline_ns);
}

long opengat_random(void *buffer, size_t length)
{
    return opengat_syscall2(OPENGAT_SYS_RANDOM, (uint64_t)(uintptr_t)buffer,
        length);
}

long opengat_random_strong(void *buffer, size_t length)
{
    return opengat_syscall2(OPENGAT_SYS_RANDOM_STRONG,
        (uint64_t)(uintptr_t)buffer, length);
}

int opengat_runtime_path(const char *input, struct opengat_runtime_path *result)
{
    if (input == NULL || result == NULL || *input == '\0') {
        errno = EINVAL;
        return -1;
    }
    result->volume = OPENGAT_VOLUME_DATA;
    result->text = input;
    if (strncmp(input, "System:", 7U) == 0) {
        result->volume = OPENGAT_VOLUME_SYSTEM;
        result->text += 7;
    } else if (strncmp(input, "Data:", 5U) == 0) {
        result->text += 5;
    }
    while (*result->text == '/') {
        ++result->text;
    }
    result->length = strlen(result->text);
    if (result->length == 0U || result->length > OPENGAT_PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

void opengat_runtime_lock(volatile uint32_t *lock)
{
    while (__atomic_exchange_n(lock, 1U, __ATOMIC_ACQUIRE) != 0U) {
        const struct opengat_futex_request request = {
            sizeof(request), OPENGAT_ABI_VERSION,
            (uint64_t)(uintptr_t)lock, 0U, 1U, 0U
        };
        (void)opengat_syscall1(OPENGAT_SYS_FUTEX_WAIT,
            (uint64_t)(uintptr_t)&request);
    }
}

void opengat_runtime_unlock(volatile uint32_t *lock)
{
    const struct opengat_futex_request request = {
        sizeof(request), OPENGAT_ABI_VERSION, (uint64_t)(uintptr_t)lock,
        0U, 0U, 1U
    };

    __atomic_store_n(lock, 0U, __ATOMIC_RELEASE);
    (void)opengat_syscall1(OPENGAT_SYS_FUTEX_WAKE,
        (uint64_t)(uintptr_t)&request);
}

int atexit(void (*function)(void))
{
    if (function == NULL) {
        errno = EINVAL;
        return -1;
    }
    opengat_runtime_lock(&exit_lock);
    if (exit_function_count == OPENGAT_ATEXIT_MAX) {
        opengat_runtime_unlock(&exit_lock);
        errno = ENOMEM;
        return -1;
    }
    exit_functions[exit_function_count++] = function;
    opengat_runtime_unlock(&exit_lock);
    return 0;
}

_Noreturn void exit(int status)
{
    (void)fflush(NULL);
    while (exit_function_count != 0U) {
        exit_functions[--exit_function_count]();
    }
    (void)opengat_syscall1(OPENGAT_SYS_EXIT, (uint64_t)(int64_t)status);
    __builtin_unreachable();
}

_Noreturn void abort(void)
{
    static const char message[] = "abort\n";

    (void)opengat_syscall2(OPENGAT_SYS_CONSOLE_WRITE,
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

void __opengat_assert(const char *expression, const char *file, int line)
{
    fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line,
        expression);
    abort();
}
