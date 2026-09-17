/* SPDX-License-Identifier: GPL-3.0-only */
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "internal.h"

#define DESCRIPTOR_MAX 32

struct descriptor_record {
    openrfs_handle_t handle;
    uint16_t volume;
    char path[OPENRFS_PATH_MAX + 1U];
    int active;
};

static struct descriptor_record descriptors[DESCRIPTOR_MAX];
static volatile uint32_t descriptor_lock;

static struct descriptor_record *descriptor(int number)
{
    return number >= 3 && number < DESCRIPTOR_MAX &&
        descriptors[number].active ? &descriptors[number] : NULL;
}

int open(const char *path, int flags, ...)
{
    struct openrfs_runtime_path parsed;
    uint32_t native = 0U;
    long handle;
    int number = -1;

    if (openrfs_runtime_path(path, &parsed) != 0) return -1;
    if ((flags & O_RDWR) == O_RDWR) native |= OPENRFS_OPEN_READ | OPENRFS_OPEN_WRITE;
    else if ((flags & O_WRONLY) != 0) native |= OPENRFS_OPEN_WRITE;
    else native |= OPENRFS_OPEN_READ;
    if ((flags & O_CREAT) != 0) native |= OPENRFS_OPEN_CREATE;
    if ((flags & O_TRUNC) != 0) native |= OPENRFS_OPEN_TRUNCATE;
    handle = openrfs_file_open(parsed.volume, parsed.text, native);
    if (handle < 0) { errno = (int)-handle; return -1; }
    openrfs_runtime_lock(&descriptor_lock);
    for (int index = 3; index < DESCRIPTOR_MAX; ++index) {
        if (!descriptors[index].active) { number = index; break; }
    }
    if (number >= 0) {
        descriptors[number].active = 1;
        descriptors[number].handle = (openrfs_handle_t)handle;
        descriptors[number].volume = parsed.volume;
        (void)memcpy(descriptors[number].path, parsed.text, parsed.length + 1U);
    }
    openrfs_runtime_unlock(&descriptor_lock);
    if (number < 0) { (void)openrfs_handle_close((openrfs_handle_t)handle); errno = EMFILE; }
    if (number >= 0 && (flags & O_APPEND) != 0 &&
        lseek(number, 0, SEEK_END) < 0) { (void)close(number); return -1; }
    return number;
}

ssize_t read(int number, void *buffer, size_t length)
{
    struct descriptor_record *record;
    long result;
    if (number == STDIN_FILENO) {
        if (length == 0U) return 0;
        errno = EAGAIN;
        return -1;
    }
    if (buffer == NULL && length != 0U) { errno = EFAULT; return -1; }
    openrfs_runtime_lock(&descriptor_lock);
    record = descriptor(number);
    if (record == NULL) {
        openrfs_runtime_unlock(&descriptor_lock);
        errno = EBADF;
        return -1;
    }
    if (length == 0U) {
        openrfs_runtime_unlock(&descriptor_lock);
        return 0;
    }
    result = openrfs_file_read(record->handle, buffer, length);
    openrfs_runtime_unlock(&descriptor_lock);
    if (result < 0) { errno = (int)-result; return -1; }
    return (ssize_t)result;
}

ssize_t write(int number, const void *buffer, size_t length)
{
    struct descriptor_record *record;
    long result;
    if (buffer == NULL && length != 0U) { errno = EFAULT; return -1; }
    if (number == STDOUT_FILENO || number == STDERR_FILENO) {
        if (length == 0U) return 0;
        result = openrfs_syscall2(OPENRFS_SYS_CONSOLE_WRITE,
            (uint64_t)(uintptr_t)buffer, length);
    } else {
        openrfs_runtime_lock(&descriptor_lock);
        record = descriptor(number);
        if (record == NULL) {
            openrfs_runtime_unlock(&descriptor_lock);
            errno = EBADF;
            return -1;
        }
        if (length == 0U) {
            openrfs_runtime_unlock(&descriptor_lock);
            return 0;
        }
        result = openrfs_file_write(record->handle, buffer, length);
        openrfs_runtime_unlock(&descriptor_lock);
    }
    if (result < 0) { errno = (int)-result; return -1; }
    return (ssize_t)result;
}

off_t lseek(int number, off_t offset, int origin)
{
    struct descriptor_record *record;
    long result;
    if (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END) {
        errno = EINVAL;
        return -1;
    }
    openrfs_runtime_lock(&descriptor_lock);
    record = descriptor(number);
    if (record == NULL) {
        openrfs_runtime_unlock(&descriptor_lock);
        errno = EBADF;
        return -1;
    }
    result = openrfs_file_seek(record->handle, offset, (uint32_t)origin);
    openrfs_runtime_unlock(&descriptor_lock);
    if (result < 0) { errno = (int)-result; return -1; }
    return (off_t)result;
}

int close(int number)
{
    struct descriptor_record *record;
    long result;
    /* File operations hold this lock through their syscall, so the descriptor
       cannot be closed and recycled while its handle or path is in use. */
    openrfs_runtime_lock(&descriptor_lock);
    record = descriptor(number);
    if (record == NULL) {
        openrfs_runtime_unlock(&descriptor_lock);
        errno = EBADF;
        return -1;
    }
    result = openrfs_handle_close(record->handle);
    if (result >= 0) {
        (void)memset(record, 0, sizeof(*record));
    }
    openrfs_runtime_unlock(&descriptor_lock);
    return openrfs_result(result);
}

int stat(const char *path, struct stat *result)
{
    struct openrfs_runtime_path parsed;
    struct openrfs_path_stat native = {sizeof(native), OPENRFS_ABI_VERSION, 0U, 0U, 0U};
    long status;
    if (result == NULL || openrfs_runtime_path(path, &parsed) != 0) return -1;
    status = openrfs_path_stat(parsed.volume, parsed.text, &native);
    if (status < 0) { errno = (int)-status; return -1; }
    result->st_size = native.byte_length;
    result->st_mode = (native.attributes & OPENRFS_PATH_DIRECTORY) != 0U ?
        S_IFDIR | S_IRUSR : S_IFREG | S_IRUSR;
    if ((native.attributes & OPENRFS_PATH_READ_ONLY) == 0U) result->st_mode |= S_IWUSR;
    return 0;
}

int access(const char *path, int mode)
{
    struct stat result;
    if ((mode & ~(R_OK | W_OK)) != 0) { errno = EINVAL; return -1; }
    if (stat(path, &result) != 0) return -1;
    if ((mode & W_OK) != 0 && (result.st_mode & S_IWUSR) == 0U) { errno = EACCES; return -1; }
    return 0;
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
int unlink(const char *path) { return path_operation(path, OPENRFS_SYS_PATH_UNLINK, 0U); }
int rmdir(const char *path) { return path_operation(path, OPENRFS_SYS_PATH_UNLINK, 0U); }
int mkdir(const char *path, mode_t mode)
{ (void)mode; return path_operation(path, OPENRFS_SYS_PATH_MKDIR, 0U); }
int ftruncate(int number, int64_t length)
{
    struct descriptor_record *record;
    long result;
    if (length < 0) { errno = EINVAL; return -1; }
    openrfs_runtime_lock(&descriptor_lock);
    record = descriptor(number);
    if (record == NULL) {
        openrfs_runtime_unlock(&descriptor_lock);
        errno = EBADF;
        return -1;
    }
    result = openrfs_path_truncate(record->volume, record->path,
        (uint64_t)length);
    openrfs_runtime_unlock(&descriptor_lock);
    return openrfs_result(result);
}
int fsync(int number)
{
    struct descriptor_record *record;
    long result;
    openrfs_runtime_lock(&descriptor_lock);
    record = descriptor(number);
    if (record == NULL) {
        openrfs_runtime_unlock(&descriptor_lock);
        errno = EBADF;
        return -1;
    }
    result = openrfs_volume_sync(record->volume);
    openrfs_runtime_unlock(&descriptor_lock);
    return openrfs_result(result);
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
int getpid(void) { return 1; }

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
    struct openrfs_directory_entry native;
    long result;
    if (directory == NULL) { errno = EBADF; return NULL; }
    result = openrfs_directory_read(directory->handle, &native);
    if (result <= 0) { if (result < 0) errno = (int)-result; return NULL; }
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
