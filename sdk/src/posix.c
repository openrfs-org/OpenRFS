/* SPDX-License-Identifier: GPL-3.0-only */
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "internal.h"

#define DESCRIPTOR_MAX 32

_Static_assert(DESCRIPTOR_MAX == OPENRFS_EXEC_DESCRIPTOR_COUNT,
    "exec descriptor count differs from the SDK");

enum descriptor_kind {
    DESCRIPTOR_NONE = 0,
    DESCRIPTOR_FILE,
    DESCRIPTOR_CONSOLE_IN,
    DESCRIPTOR_CONSOLE_OUT,
    DESCRIPTOR_PIPE_READ,
    DESCRIPTOR_PIPE_WRITE
};

struct descriptor_record {
    openrfs_handle_t handle;
    uint16_t volume;
    char path[OPENRFS_PATH_MAX + 1U];
    int open_flags;
    int descriptor_flags;
    enum descriptor_kind kind;
    int active;
};

static struct descriptor_record descriptors[DESCRIPTOR_MAX] = {
    [0] = {.kind = DESCRIPTOR_CONSOLE_IN, .active = 1},
    [1] = {.kind = DESCRIPTOR_CONSOLE_OUT, .active = 1},
    [2] = {.kind = DESCRIPTOR_CONSOLE_OUT, .active = 1}
};
static volatile uint32_t descriptor_lock;

int openrfs_posix_exec_restore(const struct openrfs_exec_descriptor *records,
    uint64_t count)
{
    if (records == NULL || count != DESCRIPTOR_MAX) return -1;
    for (size_t index = 0U; index < count; ++index) {
        const struct openrfs_exec_descriptor *source = &records[index];
        struct descriptor_record *target = &descriptors[index];

        if (source->active > 1U || source->reserved != 0U ||
                (source->descriptor_flags &
                    ~(OPENRFS_EXEC_FD_CLOEXEC |
                        OPENRFS_EXEC_FD_CLOFORK)) != 0U ||
                source->kind > OPENRFS_EXEC_DESCRIPTOR_PIPE_WRITE)
            return -1;
        (void)memset(target, 0, sizeof(*target));
        if (source->active == 0U) continue;
        if (source->kind == OPENRFS_EXEC_DESCRIPTOR_NONE) return -1;
        target->handle = source->handle;
        target->volume = source->volume;
        target->open_flags = (int)source->open_flags;
        target->descriptor_flags = (int)source->descriptor_flags;
        target->kind = (enum descriptor_kind)source->kind;
        target->active = 1;
    }
    return 0;
}

static int descriptor_snapshot(int number, struct descriptor_record *record, int retire)
{
    int found = 0;
    openrfs_runtime_lock(&descriptor_lock);
    if (number >= 0 && number < DESCRIPTOR_MAX && descriptors[number].active == 1) {
        *record = descriptors[number];
        if (retire) (void)memset(&descriptors[number], 0, sizeof(descriptors[number]));
        found = 1;
    }
    openrfs_runtime_unlock(&descriptor_lock);
    return found;
}

static int native_descriptor(enum descriptor_kind kind)
{
    return kind == DESCRIPTOR_FILE || kind == DESCRIPTOR_PIPE_READ ||
        kind == DESCRIPTOR_PIPE_WRITE;
}

static long pipe_transfer(const struct descriptor_record *record,
    void *buffer, size_t length, int write_operation)
{
    for (;;) {
        const long result = write_operation ? openrfs_file_write(record->handle,
            buffer, length) : openrfs_file_read(record->handle, buffer, length);

        if (result != -OPENRFS_EAGAIN) return result;
        const long flags = openrfs_syscall1(OPENRFS_SYS_PIPE_GET_FLAGS,
            record->handle);

        if (flags < 0) return flags;
        if ((flags & OPENRFS_PIPE_NONBLOCK) != 0) return result;
        const long ready = openrfs_syscall2(OPENRFS_SYS_PIPE_WAIT,
            record->handle, length);

        if (ready < 0) return ready;
    }
}

int open(const char *path, int flags, ...)
{
    struct openrfs_runtime_path parsed;
    uint32_t native = 0U;
    long handle;
    int number = -1;

    if (openrfs_runtime_path(path, &parsed) != 0) return -1;
    if ((flags & ~(O_RDWR | O_CREAT | O_TRUNC | O_APPEND | O_EXCL |
            O_CLOEXEC | O_CLOFORK)) != 0) { errno = EINVAL; return -1; }
    if ((flags & O_EXCL) != 0 && (flags & O_CREAT) == 0) { errno = EINVAL; return -1; }
    if ((flags & O_RDWR) == O_RDWR) native |= OPENRFS_OPEN_READ | OPENRFS_OPEN_WRITE;
    else if ((flags & O_WRONLY) != 0) native |= OPENRFS_OPEN_WRITE;
    else native |= OPENRFS_OPEN_READ;
    if ((flags & O_CREAT) != 0) native |= OPENRFS_OPEN_CREATE;
    if ((flags & O_TRUNC) != 0) native |= OPENRFS_OPEN_TRUNCATE;
    if ((flags & O_APPEND) != 0) native |= OPENRFS_OPEN_APPEND;
    if ((flags & O_EXCL) != 0) native |= OPENRFS_OPEN_EXCLUSIVE;
    // Reserve the descriptor before a native create/truncate can mutate disk.
    // State 2 is private to this open and is not a usable descriptor.
    openrfs_runtime_lock(&descriptor_lock);
    for (int index = 0; index < DESCRIPTOR_MAX; ++index) {
        if (!descriptors[index].active) { number = index; descriptors[index].active = 2; break; }
    }
    openrfs_runtime_unlock(&descriptor_lock);
    if (number < 0) { errno = EMFILE; return -1; }
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        const mode_t mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
        handle = openrfs_file_open_mode(parsed.volume, parsed.text, native, (uint16_t)(mode & 07777U));
    } else {
        handle = openrfs_file_open(parsed.volume, parsed.text, native);
    }
    openrfs_runtime_lock(&descriptor_lock);
    if (handle >= 0) {
        descriptors[number].handle = (openrfs_handle_t)handle;
        descriptors[number].volume = parsed.volume;
        (void)memcpy(descriptors[number].path, parsed.text, parsed.length + 1U);
        descriptors[number].open_flags = flags &
            (O_RDWR | O_APPEND);
        descriptors[number].descriptor_flags =
            ((flags & O_CLOEXEC) != 0 ? FD_CLOEXEC : 0) |
            ((flags & O_CLOFORK) != 0 ? FD_CLOFORK : 0);
        descriptors[number].kind = DESCRIPTOR_FILE;
        descriptors[number].active = 1;
    } else { (void)memset(&descriptors[number], 0, sizeof(descriptors[number])); }
    openrfs_runtime_unlock(&descriptor_lock);
    if (handle < 0) { errno = (int)-handle; return -1; }
    if ((flags & O_APPEND) != 0 &&
        lseek(number, 0, SEEK_END) < 0) { (void)close(number); return -1; }
    return number;
}

ssize_t read(int number, void *buffer, size_t length)
{
    struct descriptor_record record;
    long result;
    if (buffer == NULL && length != 0U) { errno = EFAULT; return -1; }
    if (!descriptor_snapshot(number, &record, 0)) { errno = EBADF; return -1; }
    if (length == 0U) return 0;
    if (record.kind == DESCRIPTOR_CONSOLE_IN) {
        result = openrfs_console_read(buffer, length);
    } else if (record.kind == DESCRIPTOR_FILE) {
        result = openrfs_file_read(record.handle, buffer, length);
    } else if (record.kind == DESCRIPTOR_PIPE_READ) {
        result = pipe_transfer(&record, buffer, length, 0);
    } else { errno = EBADF; return -1; }
    if (result < 0) { errno = (int)-result; return -1; }
    return (ssize_t)result;
}

ssize_t write(int number, const void *buffer, size_t length)
{
    struct descriptor_record record;
    long result;
    if (buffer == NULL && length != 0U) { errno = EFAULT; return -1; }
    if (!descriptor_snapshot(number, &record, 0)) { errno = EBADF; return -1; }
    if (record.kind == DESCRIPTOR_CONSOLE_OUT) {
        if (length == 0U) return 0;
        result = openrfs_syscall2(OPENRFS_SYS_CONSOLE_WRITE,
            (uint64_t)(uintptr_t)buffer, length);
    } else if (record.kind == DESCRIPTOR_FILE) {
        if (length == 0U) return 0;
        result = openrfs_file_write(record.handle, buffer, length);
    } else if (record.kind == DESCRIPTOR_PIPE_WRITE) {
        result = pipe_transfer(&record, (void *)(uintptr_t)buffer, length, 1);
    } else { errno = EBADF; return -1; }
    if (result < 0) { errno = (int)-result; return -1; }
    return (ssize_t)result;
}

off_t lseek(int number, off_t offset, int origin)
{
    struct descriptor_record record;
    long result;
    if (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END) { errno = EINVAL; return -1; }
    if (!descriptor_snapshot(number, &record, 0)) { errno = EBADF; return -1; }
    if (record.kind != DESCRIPTOR_FILE) { errno = ESPIPE; return -1; }
    result = openrfs_file_seek(record.handle, offset, (uint32_t)origin);
    if (result < 0) { errno = (int)-result; return -1; }
    return (off_t)result;
}

int close(int number)
{
    struct descriptor_record record;
    long result;
    // Retire before entering native teardown: a nested/concurrent open may
    // reuse the number, and this close must never erase that replacement.
    if (!descriptor_snapshot(number, &record, 1)) { errno = EBADF; return -1; }
    if (!native_descriptor(record.kind)) return 0;
    result = openrfs_handle_close(record.handle);
    return openrfs_result(result);
}

static int duplicate_from(int source, int minimum, int descriptor_flags)
{
    struct descriptor_record record;
    int destination = -1;

    if (minimum < 0 || minimum >= DESCRIPTOR_MAX) {
        errno = EINVAL;
        return -1;
    }
    openrfs_runtime_lock(&descriptor_lock);
    if (source < 0 || source >= DESCRIPTOR_MAX ||
        descriptors[source].active != 1) {
        openrfs_runtime_unlock(&descriptor_lock);
        errno = EBADF;
        return -1;
    }
    for (int index = minimum; index < DESCRIPTOR_MAX; ++index) {
        if (descriptors[index].active == 0) {
            destination = index;
            break;
        }
    }
    if (destination < 0) {
        openrfs_runtime_unlock(&descriptor_lock);
        errno = EMFILE;
        return -1;
    }
    record = descriptors[source];
    if (native_descriptor(record.kind)) {
        const long duplicate = openrfs_handle_duplicate(record.handle);

        if (duplicate < 0) {
            openrfs_runtime_unlock(&descriptor_lock);
            errno = duplicate == -OPENRFS_ENOMEM ? EMFILE : (int)-duplicate;
            return -1;
        }
        record.handle = (openrfs_handle_t)duplicate;
    }
    record.descriptor_flags = descriptor_flags;
    descriptors[destination] = record;
    openrfs_runtime_unlock(&descriptor_lock);
    return destination;
}

int dup(int source) { return duplicate_from(source, 0, 0); }

static int duplicate_exact(int source, int destination, int flags, int same_ok)
{
    struct descriptor_record replacement;
    struct descriptor_record previous = {0};

    if (source < 0 || source >= DESCRIPTOR_MAX ||
        destination < 0 || destination >= DESCRIPTOR_MAX) {
        errno = EBADF;
        return -1;
    }
    if ((flags & ~(O_CLOEXEC | O_CLOFORK)) != 0) {
        errno = EINVAL;
        return -1;
    }
    openrfs_runtime_lock(&descriptor_lock);
    if (descriptors[source].active != 1) {
        openrfs_runtime_unlock(&descriptor_lock);
        errno = EBADF;
        return -1;
    }
    if (source == destination) {
        openrfs_runtime_unlock(&descriptor_lock);
        if (!same_ok) { errno = EINVAL; return -1; }
        return destination;
    }
    if (descriptors[destination].active == 2) {
        openrfs_runtime_unlock(&descriptor_lock);
        errno = EBUSY;
        return -1;
    }
    replacement = descriptors[source];
    if (native_descriptor(replacement.kind)) {
        const long duplicate = openrfs_handle_duplicate(replacement.handle);

        if (duplicate < 0) {
            openrfs_runtime_unlock(&descriptor_lock);
            errno = duplicate == -OPENRFS_ENOMEM ? EMFILE : (int)-duplicate;
            return -1;
        }
        replacement.handle = (openrfs_handle_t)duplicate;
    }
    previous = descriptors[destination];
    replacement.descriptor_flags =
        ((flags & O_CLOEXEC) != 0 ? FD_CLOEXEC : 0) |
        ((flags & O_CLOFORK) != 0 ? FD_CLOFORK : 0);
    descriptors[destination] = replacement;
    openrfs_runtime_unlock(&descriptor_lock);
    if (previous.active == 1 && native_descriptor(previous.kind)) {
        (void)openrfs_handle_close(previous.handle);
    }
    return destination;
}

int dup2(int source, int destination)
{
    return duplicate_exact(source, destination, 0, 1);
}

int dup3(int source, int destination, int flags)
{
    return duplicate_exact(source, destination, flags, 0);
}

int fcntl(int number, int command, ...)
{
    int argument = 0;
    struct descriptor_record record;

    if (command == F_DUPFD || command == F_DUPFD_CLOEXEC ||
        command == F_DUPFD_CLOFORK || command == F_SETFD ||
        command == F_SETFL) {
        va_list arguments;
        va_start(arguments, command);
        argument = va_arg(arguments, int);
        va_end(arguments);
    }
    if (command == F_DUPFD || command == F_DUPFD_CLOEXEC ||
        command == F_DUPFD_CLOFORK) {
        return duplicate_from(number, argument,
            command == F_DUPFD_CLOEXEC ? FD_CLOEXEC :
                (command == F_DUPFD_CLOFORK ? FD_CLOFORK : 0));
    }
    openrfs_runtime_lock(&descriptor_lock);
    if (number < 0 || number >= DESCRIPTOR_MAX ||
        descriptors[number].active != 1) {
        openrfs_runtime_unlock(&descriptor_lock);
        errno = EBADF;
        return -1;
    }
    record = descriptors[number];
    if (command == F_SETFD) {
        if ((argument & ~(FD_CLOEXEC | FD_CLOFORK)) != 0) {
            openrfs_runtime_unlock(&descriptor_lock);
            errno = EINVAL;
            return -1;
        }
        descriptors[number].descriptor_flags = argument;
    }
    openrfs_runtime_unlock(&descriptor_lock);
    if (command == F_GETFD) return record.descriptor_flags;
    if (command == F_SETFD) return 0;
    if (command == F_GETFL) {
        if (record.kind == DESCRIPTOR_FILE) {
            const long flags = openrfs_syscall1(OPENRFS_SYS_FILE_GET_STATUS,
                record.handle);

            if (flags < 0) { errno = (int)-flags; return -1; }
            return (record.open_flags & O_ACCMODE) |
                ((flags & OPENRFS_OPEN_APPEND) != 0 ? O_APPEND : 0);
        }
        if (record.kind == DESCRIPTOR_PIPE_READ ||
            record.kind == DESCRIPTOR_PIPE_WRITE) {
            const long flags = openrfs_syscall1(OPENRFS_SYS_PIPE_GET_FLAGS,
                record.handle);

            if (flags < 0) { errno = (int)-flags; return -1; }
            return (record.kind == DESCRIPTOR_PIPE_READ ? O_RDONLY : O_WRONLY) |
                ((flags & OPENRFS_PIPE_NONBLOCK) != 0 ? O_NONBLOCK : 0);
        }
        return native_descriptor(record.kind) ? record.open_flags :
            (record.kind == DESCRIPTOR_CONSOLE_IN ? O_RDONLY : O_WRONLY);
    }
    if (command == F_SETFL && (record.kind == DESCRIPTOR_PIPE_READ ||
            record.kind == DESCRIPTOR_PIPE_WRITE)) {
        if ((argument & ~(O_ACCMODE | O_NONBLOCK)) != 0) {
            errno = EINVAL;
            return -1;
        }
        return openrfs_result(openrfs_syscall2(OPENRFS_SYS_PIPE_SET_FLAGS,
            record.handle, (argument & O_NONBLOCK) != 0 ?
                OPENRFS_PIPE_NONBLOCK : 0U));
    }
    if (command == F_SETFL && record.kind == DESCRIPTOR_FILE) {
        if ((argument & ~(O_ACCMODE | O_APPEND)) != 0) {
            errno = EINVAL;
            return -1;
        }
        return openrfs_result(openrfs_syscall2(OPENRFS_SYS_FILE_SET_STATUS,
            record.handle, (argument & O_APPEND) != 0 ?
                OPENRFS_OPEN_APPEND : 0U));
    }
    errno = command == F_SETFL ? ENOSYS : EINVAL;
    return -1;
}

static int finish_metadata(long status, struct openrfs_path_metadata native, struct stat *result)
{
    if (status < 0) { errno = (int)-status; return -1; }
    if (native.size != sizeof(native) || native.version != OPENRFS_ABI_VERSION ||
        native.atime_nanos >= 1000000000U || native.mtime_nanos >= 1000000000U ||
        native.ctime_nanos >= 1000000000U) { errno = EIO; return -1; }
    (void)memset(result, 0, sizeof(*result));
    result->st_size = native.byte_length;
    result->st_mode = native.mode;
    result->st_uid = native.uid;
    result->st_gid = native.gid;
    result->st_nlink = native.links;
    result->st_ino = native.object_id;
    result->st_atim = (struct timespec){native.atime_seconds, (long)native.atime_nanos};
    result->st_mtim = (struct timespec){native.mtime_seconds, (long)native.mtime_nanos};
    result->st_ctim = (struct timespec){native.ctime_seconds, (long)native.ctime_nanos};
    return 0;
}

static int path_metadata(const char *path, struct stat *result, uint32_t flags)
{
    struct openrfs_runtime_path parsed;
    struct openrfs_path_metadata native = {0};
    if (result == NULL) { errno = EFAULT; return -1; }
    if (openrfs_runtime_path(path, &parsed) != 0) return -1;
    const long status = openrfs_path_metadata(parsed.volume, parsed.text, flags, &native);
    return finish_metadata(status, native, result);
}

int fstat(int number, struct stat *result)
{
    struct descriptor_record record;
    struct openrfs_path_metadata native = {0};
    if (result == NULL) { errno = EFAULT; return -1; }
    if (!descriptor_snapshot(number, &record, 0)) { errno = EBADF; return -1; }
    if (record.kind != DESCRIPTOR_FILE) { errno = EBADF; return -1; }
    const long status = openrfs_file_metadata(record.handle, &native);
    return finish_metadata(status, native, result);
}

int stat(const char *path, struct stat *result) { return path_metadata(path, result, 0U); }
int lstat(const char *path, struct stat *result) { return path_metadata(path, result, OPENRFS_METADATA_NOFOLLOW); }

int access(const char *path, int mode)
{
    struct stat result;
    if ((mode & ~(R_OK | W_OK)) != 0) { errno = EINVAL; return -1; }
    if (stat(path, &result) != 0) return -1;
    if ((mode & W_OK) != 0 && (result.st_mode & S_IWUSR) == 0U) { errno = EACCES; return -1; }
    return 0;
}

int chdir(const char *path)
{
    struct openrfs_runtime_path parsed;
    struct openrfs_path request;

    if (openrfs_runtime_path(path, &parsed) != 0) return -1;
    request = (struct openrfs_path){(uint64_t)(uintptr_t)parsed.text,
        (uint32_t)parsed.length, parsed.volume, 0U};
    return openrfs_result(openrfs_syscall1(OPENRFS_SYS_PROCESS_CHDIR,
        (uint64_t)(uintptr_t)&request));
}

char *getcwd(char *buffer, size_t size)
{
    if (buffer == NULL) { errno = EFAULT; return NULL; }
    const long result = openrfs_syscall2(OPENRFS_SYS_PROCESS_GETCWD,
        (uint64_t)(uintptr_t)buffer, size);

    if (result < 0) { errno = (int)-result; return NULL; }
    return buffer;
}

static int path_operation(const char *path, uint64_t number, uint64_t value)
{
    struct openrfs_runtime_path parsed;
    struct openrfs_path request;
    long result;
    if (openrfs_runtime_path(path, &parsed) != 0) return -1;
    request = (struct openrfs_path){(uint64_t)(uintptr_t)parsed.text,
        (uint32_t)parsed.length, parsed.volume, 0U};
    result = openrfs_syscall2(number, (uint64_t)(uintptr_t)&request, value);
    return openrfs_result(result);
}
int unlink(const char *path) { return path_operation(path, OPENRFS_SYS_PATH_UNLINK, OPENRFS_UNLINK_FILE); }
int chmod(const char *path, mode_t mode) { return path_operation(path, OPENRFS_SYS_PATH_CHMOD, mode); }
int symlink(const char *target, const char *path)
{
    struct openrfs_runtime_path parsed;
    if (openrfs_runtime_path(path, &parsed) != 0) return -1;
    return openrfs_result(openrfs_path_symlink(parsed.volume, parsed.text, target));
}
int link(const char *source, const char *destination)
{
    struct openrfs_runtime_path from;
    struct openrfs_runtime_path to;
    struct openrfs_rename_request request;
    if (openrfs_runtime_path(source, &from) != 0 ||
        openrfs_runtime_path(destination, &to) != 0) return -1;
    if (from.volume == OPENRFS_VOLUME_SYSTEM ||
        to.volume == OPENRFS_VOLUME_SYSTEM) { errno = EXDEV; return -1; }
    request = (struct openrfs_rename_request){
        sizeof(request), OPENRFS_ABI_VERSION,
        {(uint64_t)(uintptr_t)from.text, (uint32_t)from.length,
            from.volume, 0U},
        {(uint64_t)(uintptr_t)to.text, (uint32_t)to.length,
            to.volume, 0U},
        0U, 0U
    };
    return openrfs_result(openrfs_syscall1(OPENRFS_SYS_PATH_LINK,
        (uint64_t)(uintptr_t)&request));
}
ssize_t readlink(const char *path, char *output, size_t capacity)
{
    struct openrfs_runtime_path parsed;
    if (openrfs_runtime_path(path, &parsed) != 0) return -1;
    return (ssize_t)openrfs_result(openrfs_path_readlink(parsed.volume, parsed.text, output, capacity));
}
int rmdir(const char *path) { return path_operation(path, OPENRFS_SYS_PATH_UNLINK, OPENRFS_UNLINK_DIRECTORY); }
int mkdir(const char *path, mode_t mode)
{
    return path_operation(path, OPENRFS_SYS_PATH_MKDIR,
        OPENRFS_MKDIR_MODE_PRESENT | (uint64_t)(mode & 07777U));
}
mode_t umask(mode_t mask)
{
    return (mode_t)openrfs_syscall1(OPENRFS_SYS_PROCESS_UMASK,
        (uint64_t)(mask & 0777U));
}
int ftruncate(int number, int64_t length)
{
    struct descriptor_record record;
    if (!descriptor_snapshot(number, &record, 0)) { errno = EBADF; return -1; }
    if (record.kind != DESCRIPTOR_FILE) { errno = EBADF; return -1; }
    if (length < 0) { errno = EINVAL; return -1; }
    return openrfs_result(openrfs_file_truncate(record.handle, (uint64_t)length));
}
int fsync(int number)
{
    struct descriptor_record record;
    if (!descriptor_snapshot(number, &record, 0)) { errno = EBADF; return -1; }
    if (record.kind != DESCRIPTOR_FILE) { errno = EBADF; return -1; }
    return openrfs_result(openrfs_file_sync(record.handle));
}
unsigned int sleep(unsigned int seconds)
{
    const struct timespec request = {(time_t)seconds, 0};
    return nanosleep(&request, NULL) == 0 ? 0U : seconds;
}
int usleep(unsigned int microseconds)
{
    const struct timespec request = {(time_t)(microseconds / 1000000U),
        (long)(microseconds % 1000000U) * 1000L};
    return nanosleep(&request, NULL);
}
int getpid(void) { return openrfs_result(openrfs_syscall0(OPENRFS_SYS_PROCESS_ID)); }
int getppid(void) { return openrfs_result(openrfs_syscall0(OPENRFS_SYS_PARENT_ID)); }
int getpgid(int pid)
{
    return openrfs_result(openrfs_syscall1(OPENRFS_SYS_PROCESS_GROUP_GET,
        (uint64_t)(int64_t)pid));
}
int getpgrp(void) { return getpgid(0); }
int setpgid(int pid, int pgid)
{
    return openrfs_result(openrfs_syscall2(OPENRFS_SYS_PROCESS_GROUP_SET,
        (uint64_t)(int64_t)pid, (uint64_t)(int64_t)pgid));
}
int setsid(void)
{
    return openrfs_result(openrfs_syscall0(
        OPENRFS_SYS_PROCESS_SESSION_CREATE));
}
int getsid(int pid)
{
    return openrfs_result(openrfs_syscall1(OPENRFS_SYS_PROCESS_SESSION_GET,
        (uint64_t)(int64_t)pid));
}
int fork(void)
{
    openrfs_runtime_lock(&descriptor_lock);
    const long native_result = openrfs_syscall0(OPENRFS_SYS_PROCESS_FORK);

    if (native_result == 0) {
        __atomic_store_n(&descriptor_lock, 0U, __ATOMIC_RELEASE);
        for (int index = 0; index < DESCRIPTOR_MAX; ++index) {
            if (descriptors[index].active == 2) {
                (void)memset(&descriptors[index], 0,
                    sizeof(descriptors[index]));
            } else if (descriptors[index].active == 1 &&
                (descriptors[index].descriptor_flags & FD_CLOFORK) != 0) {
                (void)close(index);
            }
        }
    } else {
        openrfs_runtime_unlock(&descriptor_lock);
    }
    return openrfs_result(native_result);
}
pid_t waitpid(pid_t pid, int *status, int options)
{
    return openrfs_result(openrfs_syscall3(OPENRFS_SYS_PROCESS_WAIT,
        (uint64_t)(int64_t)pid, (uint64_t)(uintptr_t)status,
        (uint64_t)(unsigned)options));
}
pid_t wait(int *status) { return waitpid(-1, status, 0); }
int kill(int pid, int signal_number)
{
    return openrfs_result(openrfs_syscall2(OPENRFS_SYS_PROCESS_SIGNAL,
        (uint64_t)(int64_t)pid, (uint64_t)(int64_t)signal_number));
}
int execve(const char *path, char *const argv[], char *const envp[])
{
    struct openrfs_exec_descriptor inherited[DESCRIPTOR_MAX] = {{0}};
    struct openrfs_exec_request request;

    if (path == NULL || argv == NULL) { errno = EFAULT; return -1; }
    if (strncmp(path, "System:", 7U) == 0) path += 7U;
    else if (strncmp(path, "Data:", 5U) == 0) {
        errno = ENOTSUP;
        return -1;
    }
    openrfs_runtime_lock(&descriptor_lock);
    for (size_t index = 0U; index < DESCRIPTOR_MAX; ++index) {
        const struct descriptor_record *source = &descriptors[index];
        struct openrfs_exec_descriptor *target = &inherited[index];

        if (source->active == 2) {
            openrfs_runtime_unlock(&descriptor_lock);
            errno = EBUSY;
            return -1;
        }
        if (source->active != 1) continue;
        target->handle = source->handle;
        target->open_flags = (uint32_t)source->open_flags;
        target->descriptor_flags = (uint32_t)source->descriptor_flags;
        target->volume = source->volume;
        target->kind = (uint8_t)source->kind;
        target->active = 1U;
    }
    request = (struct openrfs_exec_request){
        sizeof(request), OPENRFS_ABI_VERSION,
        (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)argv,
        (uint64_t)(uintptr_t)envp, (uint64_t)(uintptr_t)inherited,
        DESCRIPTOR_MAX, 0U
    };
    const long result = openrfs_syscall1(OPENRFS_SYS_PROCESS_EXEC,
        (uint64_t)(uintptr_t)&request);
    openrfs_runtime_unlock(&descriptor_lock);
    errno = result < 0 ? (int)-result : EIO;
    return -1;
}
int execv(const char *path, char *const argv[])
{
    return execve(path, argv, openrfs_startup_information()->environment);
}
int execl(const char *path, const char *arg0, ...)
{
    char *arguments[OPENRFS_EXEC_VECTOR_MAX + 1U];
    const char *argument = arg0;
    size_t count = 0U;
    va_list items;

    va_start(items, arg0);
    while (argument != NULL) {
        if (count == OPENRFS_EXEC_VECTOR_MAX) {
            va_end(items);
            errno = E2BIG;
            return -1;
        }
        arguments[count++] = (char *)argument;
        argument = va_arg(items, char *);
    }
    va_end(items);
    arguments[count] = NULL;
    return execv(path, arguments);
}
int execle(const char *path, const char *arg0, ...)
{
    char *arguments[OPENRFS_EXEC_VECTOR_MAX + 1U];
    const char *argument = arg0;
    char **environment;
    size_t count = 0U;
    va_list items;

    va_start(items, arg0);
    while (argument != NULL) {
        if (count == OPENRFS_EXEC_VECTOR_MAX) {
            va_end(items);
            errno = E2BIG;
            return -1;
        }
        arguments[count++] = (char *)argument;
        argument = va_arg(items, char *);
    }
    arguments[count] = NULL;
    environment = va_arg(items, char **);
    va_end(items);
    return execve(path, arguments, environment);
}
int pipe(int pair[2])
{
    return pipe2(pair, 0);
}
int pipe2(int pair[2], int flags)
{
    struct openrfs_pipe_pair native_pair;
    int reader = -1;
    int writer = -1;

    if (pair == NULL) { errno = EFAULT; return -1; }
    if ((flags & ~(O_NONBLOCK | O_CLOEXEC | O_CLOFORK)) != 0) {
        errno = EINVAL;
        return -1;
    }
    openrfs_runtime_lock(&descriptor_lock);
    for (int index = 0; index < DESCRIPTOR_MAX; ++index) {
        if (descriptors[index].active == 0) {
            if (reader < 0) reader = index;
            else { writer = index; break; }
        }
    }
    if (writer >= 0) {
        descriptors[reader].active = 2;
        descriptors[writer].active = 2;
    }
    if (writer < 0) {
        openrfs_runtime_unlock(&descriptor_lock);
        errno = EMFILE;
        return -1;
    }
    const long status = openrfs_syscall2(OPENRFS_SYS_PIPE_CREATE,
        (uint64_t)(uintptr_t)&native_pair,
        (flags & O_NONBLOCK) != 0 ? OPENRFS_PIPE_NONBLOCK : 0U);

    if (status == 0 && native_pair.size == sizeof(native_pair) &&
        native_pair.version == OPENRFS_ABI_VERSION) {
        descriptors[reader] = (struct descriptor_record){
            .handle = native_pair.reader,
            .open_flags = O_RDONLY | (flags & O_NONBLOCK),
            .descriptor_flags = ((flags & O_CLOEXEC) != 0 ? FD_CLOEXEC : 0) |
                ((flags & O_CLOFORK) != 0 ? FD_CLOFORK : 0),
            .kind = DESCRIPTOR_PIPE_READ, .active = 1};
        descriptors[writer] = (struct descriptor_record){
            .handle = native_pair.writer,
            .open_flags = O_WRONLY | (flags & O_NONBLOCK),
            .descriptor_flags = descriptors[reader].descriptor_flags,
            .kind = DESCRIPTOR_PIPE_WRITE, .active = 1};
    } else {
        (void)memset(&descriptors[reader], 0, sizeof(descriptors[reader]));
        (void)memset(&descriptors[writer], 0, sizeof(descriptors[writer]));
    }
    openrfs_runtime_unlock(&descriptor_lock);
    if (status < 0) { errno = (int)-status; return -1; }
    if (native_pair.size != sizeof(native_pair) ||
        native_pair.version != OPENRFS_ABI_VERSION) {
        (void)openrfs_handle_close(native_pair.reader);
        (void)openrfs_handle_close(native_pair.writer);
        errno = EIO;
        return -1;
    }
    pair[0] = reader;
    pair[1] = writer;
    return 0;
}

DIR *opendir(const char *path)
{
    struct openrfs_runtime_path parsed;
    long handle;
    DIR *result;
    if (openrfs_runtime_path(path, &parsed) != 0) return NULL;
    handle = openrfs_directory_open(parsed.volume, parsed.text);
    if (handle < 0) { errno = (int)-handle; return NULL; }
    result = calloc(1U, sizeof(*result));
    if (result == NULL) { (void)openrfs_handle_close((openrfs_handle_t)handle); return NULL; }
    result->handle = (openrfs_handle_t)handle;
    return result;
}
struct dirent *readdir(DIR *directory)
{
    struct openrfs_directory_entry_long native;
    long result;
    if (directory == NULL) { errno = EBADF; return NULL; }
    result = openrfs_directory_read_long(directory->handle, &native);
    if (result <= 0) { if (result < 0) errno = (int)-result; return NULL; }
    if (native.name_length >= sizeof(directory->entry.d_name)) { errno = EIO; return NULL; }
    (void)memcpy(directory->entry.d_name, native.name, native.name_length);
    directory->entry.d_name[native.name_length] = '\0';
    directory->entry.d_type = (native.attributes & OPENRFS_PATH_DIRECTORY) != 0U ? DT_DIR : DT_REG;
    return &directory->entry;
}
int closedir(DIR *directory)
{
    long result;
    if (directory == NULL) { errno = EBADF; return -1; }
    result = openrfs_handle_close(directory->handle);
    free(directory);
    return openrfs_result(result);
}
