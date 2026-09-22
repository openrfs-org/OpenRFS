/* SPDX-License-Identifier: GPL-3.0-only */
/* The production VFS must let a hidden journal transaction reach its retry. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/kernel/vfs.c"

static bool host_interrupts_enabled = true;
bool cpu_interrupts_enabled(void) { return host_interrupts_enabled; }
void cpu_interrupt_disable(void) { host_interrupts_enabled = false; }
void cpu_interrupt_enable(void)
{
    assert(!__atomic_load_n(&vnode_metadata_owned, __ATOMIC_RELAXED));
    host_interrupts_enabled = true;
}

static unsigned int calls;
static unsigned int stats;
static enum openrfsfs_status mutation_result = OPENRFSFS_STATUS_IO;
static const char *expected_path = "parent/file";
static bool stat_succeeds;
static unsigned live_backend_handles;
static bool directory_metadata;
static openrfsfs_handle closing_frontend;
static bool closing_directory;
static uint16_t requested_directory_mode;
static uint16_t expected_file_mode = 0644U;
static unsigned prepared_calls;
static uint8_t expected_prepared_flags = OPENRFSFS_OPEN_CREATE | OPENRFSFS_OPEN_TRUNCATE;
static unsigned file_sync_calls;
static unsigned file_stat_calls;
static unsigned publication_calls;
static unsigned held_unlink_calls;
static bool consume_last_vnode;
static unsigned unmount_calls;
static unsigned mount_calls;

static enum openrfsfs_status reentrant_mount(enum openrfsfs_volume volume)
{
    assert(volume == OPENRFSFS_VOLUME_DATA && !mounts[volume].active && mounts[volume].mounting);
    assert(!openrfsfs_resources_released());
    const unsigned before = ++mount_calls;
    openrfsfs_handle file = 99U;
    assert(openrfsfs_open(volume, "new-file", OPENRFSFS_ACCESS_READ, &file) == OPENRFSFS_STATUS_NOT_MOUNTED && file == 0U);
    assert(openrfsfs_mount(volume) == OPENRFSFS_STATUS_BUSY && mount_calls == before);
    assert(openrfsfs_unmount(volume) == OPENRFSFS_STATUS_BUSY);
    return mutation_result;
}

static struct openrfsfs_drive_info mounted_drive(enum openrfsfs_volume volume)
{
    assert(volume == OPENRFSFS_VOLUME_DATA && mounts[volume].mounting && !mounts[volume].active);
    assert(openrfsfs_mount(volume) == OPENRFSFS_STATUS_BUSY && openrfsfs_unmount(volume) == OPENRFSFS_STATUS_BUSY);
    return (struct openrfsfs_drive_info){ .mounted = true };
}

static enum openrfsfs_status reentrant_unmount(enum openrfsfs_volume volume)
{
    assert(volume == OPENRFSFS_VOLUME_DATA && !mounts[volume].active && mounts[volume].unmounting);
    assert(!openrfsfs_resources_released());
    const unsigned before = ++unmount_calls;
    openrfsfs_handle file = 99U;
    openrfsfs_directory_handle directory = 99U;
    assert(openrfsfs_open(volume, "new-file", OPENRFSFS_ACCESS_READ, &file) == OPENRFSFS_STATUS_NOT_MOUNTED && file == 0U);
    assert(openrfsfs_directory_open(volume, ".", &directory) == OPENRFSFS_STATUS_NOT_MOUNTED && directory == 0U);
    assert(openrfsfs_mount(volume) == OPENRFSFS_STATUS_BUSY);
    assert(openrfsfs_unmount(volume) == OPENRFSFS_STATUS_BUSY && unmount_calls == before);
    assert(openrfsfs_sync(volume) == OPENRFSFS_STATUS_NOT_MOUNTED);
    assert(mounts[volume].references == 0U);
    return mutation_result;
}

static enum openrfsfs_status partial_write(openrfsfs_handle handle, const uint8_t *source,
    size_t bytes, size_t *written)
{
    assert(handle == 77U && source != NULL && bytes == 4U && *written == 0U);
    *written = 2U; /* A committed prefix must remain visible on a later failure. */
    return OPENRFSFS_STATUS_IO;
}

static void stale_io_counts(openrfsfs_handle handle)
{
    uint8_t bytes[4] = { 1U, 2U, 3U, 4U };
    size_t count = 99U;
    assert(openrfsfs_read(handle, bytes, sizeof(bytes), &count) == OPENRFSFS_STATUS_STALE_HANDLE && count == 0U);
    count = 99U;
    assert(openrfsfs_pread(handle, bytes, sizeof(bytes), 1U, &count) == OPENRFSFS_STATUS_STALE_HANDLE && count == 0U);
    count = 99U;
    assert(openrfsfs_write(handle, bytes, sizeof(bytes), &count) == OPENRFSFS_STATUS_STALE_HANDLE && count == 0U);
    assert(bytes[0] == 1U && bytes[1] == 2U && bytes[2] == 3U && bytes[3] == 4U);
}

static enum openrfsfs_status read_attribute(enum openrfsfs_volume volume, const char *path,
    const char *name, uint8_t *output, size_t capacity, size_t *length)
{
    assert(volume == OPENRFSFS_VOLUME_DATA && strcmp(path, "parent/file") == 0);
    assert(openrfsfs_unmount(volume) == OPENRFSFS_STATUS_BUSY && cpu_interrupts_enabled());
    assert(strcmp(name, "user.test") == 0 && output != NULL && capacity == 4U);
    *length = 3U;
    return mutation_result;
}

static enum openrfsfs_status held_unlink(openrfsfs_handle handle, const char *path)
{
    assert(handle == 77U && live_backend_handles == 1U && strcmp(path, expected_path) == 0);
    ++held_unlink_calls;
    return mutation_result;
}

static enum openrfsfs_status publish_file(openrfsfs_handle handle, const char *source, const char *destination)
{
    assert(handle == 77U && live_backend_handles == 1U);
    assert(strcmp(source, expected_path) == 0 && strcmp(destination, "other/target") == 0);
    ++publication_calls;
    return mutation_result;
}

static enum openrfsfs_status file_stat(openrfsfs_handle handle, struct openrfsfs_stat *result)
{
    assert(handle == 77U && live_backend_handles == 1U);
    ++file_stat_calls;
    if (mutation_result == OPENRFSFS_STATUS_OK) {
        result->object_id = 500U;
        result->size = 777U;
        result->mode = 0100600U;
    }
    return mutation_result;
}

static enum openrfsfs_status file_sync(openrfsfs_handle handle)
{
    assert(handle == 77U && live_backend_handles == 1U);
    ++file_sync_calls;
    return mutation_result;
}

static enum openrfsfs_status prepared_open(enum openrfsfs_volume volume, const char *path,
    enum openrfsfs_access access, uint8_t flags, uint16_t mode,
    openrfsfs_handle *handle, struct openrfsfs_stat *result)
{
    assert(volume == OPENRFSFS_VOLUME_DATA && strcmp(path, expected_path) == 0);
    assert(access == OPENRFSFS_ACCESS_READ_WRITE && flags == expected_prepared_flags);
    assert(mode == 01720U);
    ++prepared_calls;
    if (consume_last_vnode) {
        struct openrfsfs_stat other = { .object_id = 99999U };
        size_t index;
        assert(vnode_retain(volume, "callback", &other, &index) == OPENRFSFS_STATUS_NO_HANDLES);
    }
    if (mutation_result != OPENRFSFS_STATUS_OK) return mutation_result;
    *handle = 77U;
    memset(result, 0, sizeof(*result));
    result->object_id = 500U;
    ++live_backend_handles;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status replaced_open(enum openrfsfs_volume volume, const char *path,
    enum openrfsfs_access access, openrfsfs_handle *handle, struct openrfsfs_stat *stat)
{
    assert(volume == OPENRFSFS_VOLUME_DATA && strcmp(path, expected_path) == 0);
    assert(access == OPENRFSFS_ACCESS_READ);
    *handle = 77U;
    memset(stat, 0, sizeof(*stat));
    stat->object_id = 500U; /* The name was replaced after the preliminary stat. */
    ++live_backend_handles;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status replaced_close(openrfsfs_handle handle)
{
    assert(handle == 77U && live_backend_handles == 1U);
    for (size_t index = 0U; index < VFS_MAX_VNODES; ++index) {
        assert(!vnodes[index].active || vnodes[index].stat.object_id != 500U);
    }
    if (closing_frontend != 0U) {
        const openrfsfs_handle retired = closing_frontend;
        closing_frontend = 0U;
        assert((closing_directory ? openrfsfs_directory_close(retired) : openrfsfs_close(retired)) == OPENRFSFS_STATUS_STALE_HANDLE);
    }
    --live_backend_handles;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status replaced_directory_open(enum openrfsfs_volume volume, const char *path,
    openrfsfs_handle *handle, struct openrfsfs_stat *stat)
{
    const enum openrfsfs_status status = replaced_open(volume, path, OPENRFSFS_ACCESS_READ, handle, stat);
    stat->directory = true;
    return status;
}

static enum openrfsfs_status replaced_directory_read(openrfsfs_handle handle,
    struct openrfsfs_list_entry *entry, bool *present)
{
    assert(handle == 77U && live_backend_handles == 1U && entry != NULL);
    *present = false;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status hidden_stat(enum openrfsfs_volume volume,
    const char *path, struct openrfsfs_stat *result)
{
    assert(openrfsfs_unmount(volume) == OPENRFSFS_STATUS_BUSY && cpu_interrupts_enabled());
    ++stats;
    if (stat_succeeds) {
        assert(strcmp(path, expected_path) == 0);
        memset(result, 0, sizeof(*result));
        result->object_id = 101U;
        result->directory = directory_metadata;
        return OPENRFSFS_STATUS_OK;
    }
    return OPENRFSFS_STATUS_IO;
}

static enum openrfsfs_status mutation(enum openrfsfs_volume volume, const char *path)
{
    assert(volume == OPENRFSFS_VOLUME_DATA && strcmp(path, expected_path) == 0);
    assert(openrfsfs_unmount(volume) == OPENRFSFS_STATUS_BUSY && cpu_interrupts_enabled());
    ++calls;
    return mutation_result;
}

static enum openrfsfs_status nofollow_stat(enum openrfsfs_volume volume, const char *path,
    struct openrfsfs_stat *result)
{
    const enum openrfsfs_status status = mutation(volume, path);
    if (status == OPENRFSFS_STATUS_OK) {
        result->object_id = 84U;
        result->mode = 0120777U;
        result->uid = 70000U;
        result->atime_seconds = -1;
        result->atime_nanos = 123U;
    }
    return status;
}

static enum openrfsfs_status pair(enum openrfsfs_volume volume, const char *from, const char *to)
{
    assert(strcmp(to, "other/target") == 0);
    return mutation(volume, from);
}

static enum openrfsfs_status create(enum openrfsfs_volume volume, const char *path, uint16_t mode)
{
    assert(mode == expected_file_mode);
    return mutation(volume, path);
}

static enum openrfsfs_status mkdir_mode(enum openrfsfs_volume volume, const char *path, uint16_t mode)
{
    requested_directory_mode = mode;
    return mutation(volume, path);
}

static enum openrfsfs_status truncate_file(enum openrfsfs_volume volume, const char *path, uint64_t size)
{
    assert(size == 7U);
    return mutation(volume, path);
}

static bool nested_open;
static bool fail_outer_open;
static bool opening_directory;
static openrfsfs_handle nested_handle;
static unsigned nested_live;

static enum openrfsfs_status nested_backend_open(enum openrfsfs_volume volume, const char *path,
    enum openrfsfs_access access, uint8_t flags, uint16_t mode,
    openrfsfs_handle *handle, struct openrfsfs_stat *result)
{
    (void)path; (void)access; (void)flags; (void)mode;
    const bool outer = !nested_open;
    if (outer) {
        nested_open = true;
        assert(openrfsfs_unmount(volume) == OPENRFSFS_STATUS_BUSY);
        assert((opening_directory ? openrfsfs_directory_open(volume, expected_path, &nested_handle) :
            openrfsfs_open(volume, expected_path, OPENRFSFS_ACCESS_READ, &nested_handle)) == OPENRFSFS_STATUS_OK);
        if (fail_outer_open) return OPENRFSFS_STATUS_IO;
    }
    *handle = outer ? 81U : 82U;
    memset(result, 0, sizeof(*result));
    result->object_id = *handle;
    result->directory = opening_directory;
    ++nested_live;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status nested_directory_open(enum openrfsfs_volume volume, const char *path,
    openrfsfs_handle *handle, struct openrfsfs_stat *result)
{
    return nested_backend_open(volume, path, OPENRFSFS_ACCESS_READ, 0U, 0U, handle, result);
}

static enum openrfsfs_status nested_backend_close(openrfsfs_handle handle)
{
    assert((handle == 81U || handle == 82U) && nested_live != 0U);
    --nested_live;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status unexpected_unmount(enum openrfsfs_volume volume)
{
    (void)volume;
    return OPENRFSFS_STATUS_IO;
}

static void nested_open_reservations(void)
{
    static const struct vfs_backend_ops backend = {
        .open_options = nested_backend_open, .close = nested_backend_close,
        .unmount = unexpected_unmount,
        .stat_path = hidden_stat, .case_sensitive = true, .validates_mutation_paths = true,
        .directory_open_with_stat = nested_directory_open,
        .directory_read = replaced_directory_read, .directory_close = nested_backend_close,
    };
    mounts[OPENRFSFS_VOLUME_DATA].backend = &backend;
    stat_succeeds = true;
    for (unsigned kind = 0U; kind < 2U; ++kind) {
        opening_directory = directory_metadata = kind != 0U;
        for (unsigned failure = 0U; failure < 2U; ++failure) {
            nested_open = false;
            fail_outer_open = failure != 0U;
            openrfsfs_handle outer = 0U;
            const enum openrfsfs_status expected = failure != 0U ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
            assert((opening_directory ? openrfsfs_directory_open(OPENRFSFS_VOLUME_DATA, expected_path, &outer) :
                openrfsfs_open(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_READ, &outer)) == expected);
            if (failure == 0U) {
                assert((outer & 0xffU) != (nested_handle & 0xffU));
                assert((opening_directory ? openrfsfs_directory_close(outer) : openrfsfs_close(outer)) == OPENRFSFS_STATUS_OK);
            } else assert(outer == 0U);
            assert((opening_directory ? openrfsfs_directory_close(nested_handle) : openrfsfs_close(nested_handle)) == OPENRFSFS_STATUS_OK);
            assert(nested_live == 0U && mounts[OPENRFSFS_VOLUME_DATA].references == 0U);
            for (size_t index = 0U; index < VFS_MAX_VNODES; ++index) assert(!vnodes[index].active);
            for (size_t index = 0U; index < VFS_MAX_OPEN_FILES; ++index)
                assert(!open_files[index].active && !open_files[index].opening && !open_file_claims[index]);
            for (size_t index = 0U; index < VFS_MAX_DIRECTORY_ITERATORS; ++index)
                assert(!directories[index].active && !directories[index].opening && !directory_claims[index]);
        }
    }
}

static openrfsfs_handle callback_old;
static openrfsfs_handle callback_new;
static openrfsfs_handle callback_token;
static unsigned callback_writes;
static bool callback_directory;

static enum openrfsfs_status callback_open(enum openrfsfs_volume volume, const char *path,
    enum openrfsfs_access access, uint8_t flags, uint16_t mode,
    openrfsfs_handle *handle, struct openrfsfs_stat *result)
{
    (void)volume; (void)path; (void)access; (void)flags; (void)mode;
    assert(cpu_interrupts_enabled());
    *handle = ++callback_token;
    *result = (struct openrfsfs_stat){ .object_id = *handle, .directory = callback_directory };
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status callback_close(openrfsfs_handle handle)
{
    assert(handle == callback_token && cpu_interrupts_enabled());
    assert(openrfsfs_unmount(OPENRFSFS_VOLUME_DATA) == OPENRFSFS_STATUS_BUSY);
    for (size_t index = 0U; index < VFS_MAX_VNODES; ++index)
        assert(!vnodes[index].active);
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status callback_seek(openrfsfs_handle handle, int64_t offset,
    enum openrfsfs_seek_origin origin, uint64_t *position)
{
    assert(handle == callback_token && offset == 0 && origin == OPENRFSFS_SEEK_END);
    assert(cpu_interrupts_enabled());
    assert(openrfsfs_close(callback_old) == OPENRFSFS_STATUS_OK);
    assert(openrfsfs_unmount(OPENRFSFS_VOLUME_DATA) == OPENRFSFS_STATUS_BUSY);
    assert(openrfsfs_open(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_WRITE, &callback_new) == OPENRFSFS_STATUS_OK);
    assert((callback_old & 0xffU) == (callback_new & 0xffU) && callback_old != callback_new);
    *position = 0U;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status callback_write(openrfsfs_handle handle, const uint8_t *source,
    size_t bytes, size_t *written)
{
    assert(source != NULL && bytes == 1U && *written == 0U && cpu_interrupts_enabled());
    ++callback_writes;
    /* The fallback append must retain the closed token, never the replacement. */
    assert(handle + 1U == callback_token);
    return OPENRFSFS_STATUS_STALE_HANDLE;
}

static enum openrfsfs_status callback_directory_open(enum openrfsfs_volume volume, const char *path,
    openrfsfs_handle *handle, struct openrfsfs_stat *stat)
{
    return callback_open(volume, path, OPENRFSFS_ACCESS_READ, 0U, 0U, handle, stat);
}

static enum openrfsfs_status callback_directory_read(openrfsfs_handle handle,
    struct openrfsfs_list_entry *entry, bool *present)
{
    assert(handle == callback_token && cpu_interrupts_enabled() && !*present && entry->size == 0U);
    assert(openrfsfs_directory_close(callback_old) == OPENRFSFS_STATUS_OK);
    assert(openrfsfs_unmount(OPENRFSFS_VOLUME_DATA) == OPENRFSFS_STATUS_BUSY);
    assert(openrfsfs_directory_open(OPENRFSFS_VOLUME_DATA, expected_path, &callback_new) == OPENRFSFS_STATUS_OK);
    assert((callback_old & 0xffU) == (callback_new & 0xffU) && callback_old != callback_new);
    assert(handle + 1U == callback_token);
    return OPENRFSFS_STATUS_STALE_HANDLE;
}

static void callback_slot_reuse(void)
{
    static const struct vfs_backend_ops backend = {
        .open_options = callback_open, .close = callback_close,
        .seek = callback_seek, .write = callback_write, .unmount = unexpected_unmount,
        .stat_path = hidden_stat, .directory_open_with_stat = callback_directory_open,
        .directory_read = callback_directory_read, .directory_close = callback_close,
        .case_sensitive = true, .validates_mutation_paths = true,
    };
    mounts[OPENRFSFS_VOLUME_DATA].backend = &backend;
    callback_token = 900U;
    assert(openrfsfs_open(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_WRITE, &callback_old) == OPENRFSFS_STATUS_OK);
    assert(openrfsfs_set_append(callback_old, true) == OPENRFSFS_STATUS_OK);
    const uint8_t byte = 1U;
    size_t written = 99U;
    assert(openrfsfs_write(callback_old, NULL, 1U, &written) == OPENRFSFS_STATUS_INVALID_ARGUMENT && written == 0U);
    assert(openrfsfs_write(callback_old, &byte, 1U, NULL) == OPENRFSFS_STATUS_INVALID_ARGUMENT);
    assert(openrfsfs_write(callback_old, NULL, 0U, &written) == OPENRFSFS_STATUS_OK && written == 0U);
    assert(mounts[OPENRFSFS_VOLUME_DATA].references == 1U);
    mounts[OPENRFSFS_VOLUME_DATA].references = SIZE_MAX;
    assert(openrfsfs_write(callback_old, &byte, 1U, &written) == OPENRFSFS_STATUS_BUSY && written == 0U);
    uint64_t refused_position = 99U;
    assert(openrfsfs_seek(callback_old, 0, OPENRFSFS_SEEK_START, &refused_position) == OPENRFSFS_STATUS_BUSY && refused_position == 0U);
    assert(openrfsfs_close(callback_old) == OPENRFSFS_STATUS_BUSY);
    assert(mounts[OPENRFSFS_VOLUME_DATA].references == SIZE_MAX && callback_writes == 0U);
    mounts[OPENRFSFS_VOLUME_DATA].references = 1U;
    assert(openrfsfs_write(callback_old, &byte, 1U, &written) == OPENRFSFS_STATUS_STALE_HANDLE);
    assert(written == 0U && callback_writes == 1U && mounts[OPENRFSFS_VOLUME_DATA].references == 1U);
    assert(openrfsfs_set_append(callback_old, false) == OPENRFSFS_STATUS_STALE_HANDLE);
    assert(openrfsfs_close(callback_new) == OPENRFSFS_STATUS_OK);
    assert(mounts[OPENRFSFS_VOLUME_DATA].references == 0U);
    puts("VFS callback close/reopen preserves the original token and mount without writing the replacement: PASS");
    callback_directory = directory_metadata = stat_succeeds = true;
    assert(openrfsfs_directory_open(OPENRFSFS_VOLUME_DATA, expected_path, &callback_old) == OPENRFSFS_STATUS_OK);
    struct openrfsfs_list_entry entry = { .size = 99U };
    bool present = true;
    mounts[OPENRFSFS_VOLUME_DATA].references = SIZE_MAX;
    assert(openrfsfs_directory_read(callback_old, &entry, &present) == OPENRFSFS_STATUS_BUSY && !present && entry.size == 0U);
    assert(openrfsfs_directory_close(callback_old) == OPENRFSFS_STATUS_BUSY);
    mounts[OPENRFSFS_VOLUME_DATA].references = 1U;
    assert(openrfsfs_directory_read(callback_old, &entry, &present) == OPENRFSFS_STATUS_STALE_HANDLE && !present);
    assert(mounts[OPENRFSFS_VOLUME_DATA].references == 1U);
    assert(openrfsfs_directory_read(callback_old, &entry, &present) == OPENRFSFS_STATUS_STALE_HANDLE && !present);
    assert(openrfsfs_directory_close(callback_new) == OPENRFSFS_STATUS_OK);
    assert(mounts[OPENRFSFS_VOLUME_DATA].references == 0U);
    puts("VFS streaming directory callback retirement pins the mount, refuses overflow and preserves slot generations: PASS");
}

int main(void)
{
    for (size_t index = 0U; index < VFS_VNODE_BUCKETS; ++index) vnode_buckets[index] = VFS_NO_INDEX;
    struct vfs_backend_ops backend = {
        .stat_path = hidden_stat, .create = create, .truncate = truncate_file,
        .mkdir = mutation, .unlink = mutation, .rmdir = mutation,
        .link = pair, .rename = pair, .rename_replace = pair,
        .case_sensitive = true, .validates_mutation_paths = true,
        .remove = mutation, .chmod = create,
    };
    mounts[OPENRFSFS_VOLUME_DATA].active = true;
    mounts[OPENRFSFS_VOLUME_DATA].backend = &backend;
    for (unsigned int attempt = 0U; attempt < 2U; ++attempt) {
        mutation_result = attempt == 0U ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
        assert(openrfsfs_create(OPENRFSFS_VOLUME_DATA, "parent/file") == mutation_result);
        assert(openrfsfs_truncate(OPENRFSFS_VOLUME_DATA, "parent/file", 7U) == mutation_result);
        assert(openrfsfs_mkdir(OPENRFSFS_VOLUME_DATA, "parent/file") == mutation_result);
        assert(openrfsfs_unlink(OPENRFSFS_VOLUME_DATA, "parent/file") == mutation_result);
        assert(openrfsfs_remove(OPENRFSFS_VOLUME_DATA, "parent/file") == mutation_result);
        assert(openrfsfs_rmdir(OPENRFSFS_VOLUME_DATA, "parent/file") == mutation_result);
        assert(openrfsfs_link(OPENRFSFS_VOLUME_DATA, "parent/file", "other/target") == mutation_result);
        assert(openrfsfs_rename(OPENRFSFS_VOLUME_DATA, "parent/file", "other/target") == mutation_result);
        assert(openrfsfs_rename_replace(OPENRFSFS_VOLUME_DATA, "parent/file", "other/target") == mutation_result);
        assert(mounts[OPENRFSFS_VOLUME_DATA].references == 0U);
    }
    assert(calls == 18U && stats == 0U);
    assert(openrfsfs_unlink(OPENRFSFS_VOLUME_DATA, "../parent/file") == OPENRFSFS_STATUS_PATH);
    assert(openrfsfs_unlink(OPENRFSFS_VOLUME_DATA, ".") == OPENRFSFS_STATUS_ACCESS);
    assert(calls == 18U);
    mutation_result = OPENRFSFS_STATUS_NOT_DIRECTORY;
    assert(openrfsfs_create(OPENRFSFS_VOLUME_DATA, "parent/file") == OPENRFSFS_STATUS_NOT_DIRECTORY);
    mutation_result = OPENRFSFS_STATUS_NOT_FOUND;
    assert(openrfsfs_link(OPENRFSFS_VOLUME_DATA, "parent/file", "other/target") == OPENRFSFS_STATUS_NOT_FOUND);
    backend.validates_mutation_paths = false;
    assert(openrfsfs_create(OPENRFSFS_VOLUME_DATA, "parent/file") == OPENRFSFS_STATUS_IO);
    assert(calls == 20U && stats == 1U);
    backend.validates_mutation_paths = true;
    for (size_t index = 0U; index < 3U; ++index) {
        const char *paths[] = {"parent/link/../file", "parent/missing/../file", "parent/file/./child"};
        expected_path = paths[index];
        mutation_result = index == 2U ? OPENRFSFS_STATUS_NOT_DIRECTORY : OPENRFSFS_STATUS_NOT_FOUND;
        assert(openrfsfs_create(OPENRFSFS_VOLUME_DATA, expected_path) == mutation_result);
        assert(openrfsfs_unlink(OPENRFSFS_VOLUME_DATA, expected_path) == mutation_result);
    }
    stat_succeeds = true;
    expected_path = "parent/link/../file";
    struct openrfsfs_stat metadata;
    assert(openrfsfs_stat_path(OPENRFSFS_VOLUME_DATA, expected_path, &metadata) == OPENRFSFS_STATUS_OK);
    assert(metadata.object_id == 101U && stats == 2U);
    expected_path = ".";
    mutation_result = OPENRFSFS_STATUS_OK;
    assert(openrfsfs_chmod(OPENRFSFS_VOLUME_DATA, expected_path, 0644U) == OPENRFSFS_STATUS_OK);
    assert(openrfsfs_create(OPENRFSFS_VOLUME_DATA, expected_path) == OPENRFSFS_STATUS_ACCESS);
    expected_path = "parent/caf\xc3\xa9";
    assert(openrfsfs_create(OPENRFSFS_VOLUME_DATA, expected_path) == OPENRFSFS_STATUS_OK);
    backend.open_with_stat = replaced_open;
    backend.close = replaced_close;
    expected_path = "parent/file";
    size_t old_vnode;
    memset(&metadata, 0, sizeof(metadata));
    metadata.object_id = 101U;
    assert(vnode_retain(OPENRFSFS_VOLUME_DATA, expected_path, &metadata, &old_vnode) == OPENRFSFS_STATUS_OK);
    openrfsfs_handle opened;
    assert(openrfsfs_open(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_READ, &opened) == OPENRFSFS_STATUS_OK);
    struct vfs_open_file_state *file;
    assert(open_file_state(opened, &file) == OPENRFSFS_STATUS_OK);
    assert(vnodes[file->vnode_index].stat.object_id == 500U);
    assert(vnodes[old_vnode].stat.object_id == 101U && vnodes[old_vnode].references == 1U);
    closing_frontend = opened;
    assert(openrfsfs_close(opened) == OPENRFSFS_STATUS_OK && live_backend_handles == 0U);
    assert(closing_frontend == 0U);
    /* Exhaustion after backend open must release that handle, preserving the
     * already-held old inode and every unrelated vnode reference. */
    size_t retained[VFS_MAX_VNODES - 1U];
    for (size_t index = 0U; index < VFS_MAX_VNODES - 1U; ++index) {
        metadata.object_id = 1000U + index;
        assert(vnode_retain(OPENRFSFS_VOLUME_DATA, "unrelated", &metadata, &retained[index]) == OPENRFSFS_STATUS_OK);
    }
    assert(openrfsfs_open(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_READ, &opened) == OPENRFSFS_STATUS_NO_HANDLES);
    assert(opened == 0U && live_backend_handles == 0U);
    assert(vnodes[old_vnode].references == 1U);
    for (size_t index = 0U; index < VFS_MAX_VNODES - 1U; ++index)
        vnode_release(retained[index], vnodes[retained[index]].generation);
    vnode_release(old_vnode, vnodes[old_vnode].generation);
    directory_metadata = true;
    backend.directory_open_with_stat = replaced_directory_open;
    backend.directory_read = replaced_directory_read;
    backend.directory_close = replaced_close;
    metadata.object_id = 101U;
    metadata.directory = true;
    assert(vnode_retain(OPENRFSFS_VOLUME_DATA, expected_path, &metadata, &old_vnode) == OPENRFSFS_STATUS_OK);
    openrfsfs_directory_handle directory;
    assert(openrfsfs_directory_open(OPENRFSFS_VOLUME_DATA, expected_path, &directory) == OPENRFSFS_STATUS_OK);
    struct vfs_directory_state *snapshot;
    assert(directory_state(directory, &snapshot) == OPENRFSFS_STATUS_OK);
    assert(vnodes[snapshot->vnode_index].stat.object_id == 500U);
    assert(vnodes[old_vnode].references == 1U);
    struct openrfsfs_list_entry entry;
    bool present;
    stat_succeeds = false; /* The old name can disappear without affecting iteration. */
    assert(openrfsfs_directory_read(directory, &entry, &present) == OPENRFSFS_STATUS_OK && !present);
    ++mounts[OPENRFSFS_VOLUME_DATA].generation;
    present = true;
    memset(&entry, 0x55, sizeof(entry));
    assert(openrfsfs_directory_read(directory, &entry, &present) == OPENRFSFS_STATUS_STALE_HANDLE);
    assert(!present && entry.object_id == 0U && entry.name[0] == '\0');
    --mounts[OPENRFSFS_VOLUME_DATA].generation;
    assert(openrfsfs_directory_read(directory, &entry, &present) == OPENRFSFS_STATUS_OK && !present);
    closing_frontend = directory;
    closing_directory = true;
    assert(openrfsfs_directory_close(directory) == OPENRFSFS_STATUS_OK && live_backend_handles == 0U);
    assert(closing_frontend == 0U);
    vnode_release(old_vnode, vnodes[old_vnode].generation);
    assert(mounts[OPENRFSFS_VOLUME_DATA].references == 0U);
    for (size_t index = 0U; index < VFS_MAX_VNODES; ++index) assert(!vnodes[index].active);
    backend.mkdir_mode = mkdir_mode;
    for (unsigned attempt = 0U; attempt < 2U; ++attempt) {
        mutation_result = attempt == 0U ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
        assert(openrfsfs_mkdir_mode(OPENRFSFS_VOLUME_DATA, expected_path, 01720U) == mutation_result);
        assert(requested_directory_mode == 01720U);
    }
    assert(openrfsfs_mkdir_mode(OPENRFSFS_VOLUME_DATA, expected_path, 0U) == OPENRFSFS_STATUS_OK);
    assert(requested_directory_mode == 0U);
    assert(openrfsfs_mkdir(OPENRFSFS_VOLUME_DATA, expected_path) == OPENRFSFS_STATUS_OK);
    assert(requested_directory_mode == 0755U);
    const unsigned before_invalid_mkdir = calls;
    assert(openrfsfs_mkdir_mode(OPENRFSFS_VOLUME_DATA, expected_path, 010000U) == OPENRFSFS_STATUS_INVALID_ARGUMENT);
    assert(calls == before_invalid_mkdir);
    expected_file_mode = 0U;
    assert(openrfsfs_create_mode(OPENRFSFS_VOLUME_DATA, expected_path, 0U) == OPENRFSFS_STATUS_OK);
    expected_file_mode = 07777U;
    assert(openrfsfs_create_mode(OPENRFSFS_VOLUME_DATA, expected_path, 07777U) == OPENRFSFS_STATUS_OK);
    const unsigned before_invalid_create = calls;
    assert(openrfsfs_create_mode(OPENRFSFS_VOLUME_DATA, expected_path, 010000U) == OPENRFSFS_STATUS_INVALID_ARGUMENT);
    assert(calls == before_invalid_create);
    backend.lstat_path = nofollow_stat;
    expected_path = "parent/link/../file";
    const unsigned before_lstat = stats;
    char too_long[OPENRFSFS_MAX_PATH + 1U];
    memset(too_long, 'a', sizeof(too_long));
    too_long[OPENRFSFS_MAX_PATH] = '\0';
    assert(openrfsfs_lstat_path(OPENRFSFS_VOLUME_DATA, too_long, &metadata) == OPENRFSFS_STATUS_NAME_TOO_LONG);
    assert(openrfsfs_lstat_path(OPENRFSFS_VOLUME_DATA, expected_path, &metadata) == OPENRFSFS_STATUS_OK);
    assert(metadata.object_id == 84U && metadata.mode == 0120777U && metadata.uid == 70000U);
    assert(metadata.atime_seconds == -1 && metadata.atime_nanos == 123U);
    mutation_result = OPENRFSFS_STATUS_NOT_FOUND;
    assert(openrfsfs_lstat_path(OPENRFSFS_VOLUME_DATA, expected_path, &metadata) == OPENRFSFS_STATUS_NOT_FOUND);
    assert(metadata.object_id == 0U && metadata.mode == 0U && metadata.atime_seconds == 0);
    assert(stats == before_lstat && mounts[OPENRFSFS_VOLUME_DATA].references == 0U);
    backend.open_options = prepared_open;
    const unsigned before_prepared_stats = stats;
    for (unsigned attempt = 0U; attempt < 2U; ++attempt) {
        mutation_result = attempt == 0U ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
        assert(openrfsfs_open_options(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_READ_WRITE,
            OPENRFSFS_OPEN_CREATE | OPENRFSFS_OPEN_TRUNCATE, 01720U, &opened) == mutation_result);
        if (attempt == 0U) assert(opened == 0U && live_backend_handles == 0U);
        else {
            assert(open_file_state(opened, &file) == OPENRFSFS_STATUS_OK);
            assert(vnodes[file->vnode_index].stat.object_id == 500U);
            assert(openrfsfs_close(opened) == OPENRFSFS_STATUS_OK && live_backend_handles == 0U);
        }
    }
    assert(prepared_calls == 2U && stats == before_prepared_stats);
    assert(openrfsfs_open_options(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_READ,
        OPENRFSFS_OPEN_TRUNCATE, 01720U, &opened) == OPENRFSFS_STATUS_ACCESS);
    assert(opened == 0U && prepared_calls == 2U);
    for (size_t index = 0U; index < VFS_MAX_OPEN_FILES; ++index) {
        assert(openrfs_slot_claim(open_file_claims, VFS_MAX_OPEN_FILES) != VFS_MAX_OPEN_FILES);
    }
    assert(openrfsfs_open_options(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_READ_WRITE,
        OPENRFSFS_OPEN_CREATE | OPENRFSFS_OPEN_TRUNCATE, 01720U, &opened) == OPENRFSFS_STATUS_NO_HANDLES);
    assert(opened == 0U && prepared_calls == 2U);
    for (size_t index = 0U; index < VFS_MAX_OPEN_FILES; ++index) openrfs_slot_release(open_file_claims, index);
    expected_prepared_flags = OPENRFSFS_OPEN_CREATE | OPENRFSFS_OPEN_EXCLUSIVE;
    mutation_result = OPENRFSFS_STATUS_EXISTS;
    assert(openrfsfs_open_options(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_READ_WRITE,
        expected_prepared_flags, 01720U, &opened) == OPENRFSFS_STATUS_EXISTS);
    assert(opened == 0U && prepared_calls == 3U && live_backend_handles == 0U);
    assert(openrfsfs_open_options(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_READ_WRITE,
        OPENRFSFS_OPEN_EXCLUSIVE, 01720U, &opened) == OPENRFSFS_STATUS_INVALID_ARGUMENT);
    assert(opened == 0U && prepared_calls == 3U);
    backend.fsync = file_sync;
    backend.fstat = file_stat;
    backend.publish_file = publish_file;
    backend.unlink_held_file = held_unlink;
    mutation_result = OPENRFSFS_STATUS_OK;
    assert(openrfsfs_open_options(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_READ_WRITE,
        expected_prepared_flags, 01720U, &opened) == OPENRFSFS_STATUS_OK);
    mutation_result = OPENRFSFS_STATUS_IO;
    assert(openrfsfs_fsync(opened) == OPENRFSFS_STATUS_IO);
    mutation_result = OPENRFSFS_STATUS_OK;
    assert(openrfsfs_fsync(opened) == OPENRFSFS_STATUS_OK && file_sync_calls == 2U);
    const unsigned before_file_stat = stats;
    assert(openrfsfs_fstat(opened, &metadata) == OPENRFSFS_STATUS_OK);
    assert(metadata.object_id == 500U && metadata.size == 777U && metadata.mode == 0100600U);
    assert(stats == before_file_stat && file_stat_calls == 1U);
    mutation_result = OPENRFSFS_STATUS_IO;
    assert(openrfsfs_publish_file(opened, expected_path, "other/target") == OPENRFSFS_STATUS_IO);
    mutation_result = OPENRFSFS_STATUS_OK;
    assert(openrfsfs_publish_file(opened, expected_path, "other/target") == OPENRFSFS_STATUS_OK);
    assert(publication_calls == 2U && stats == before_file_stat);
    mutation_result = OPENRFSFS_STATUS_IO;
    assert(openrfsfs_unlink_held_file(opened, expected_path) == OPENRFSFS_STATUS_IO);
    mutation_result = OPENRFSFS_STATUS_OK;
    assert(openrfsfs_unlink_held_file(opened, expected_path) == OPENRFSFS_STATUS_OK);
    assert(held_unlink_calls == 2U && stats == before_file_stat);
    ++mounts[OPENRFSFS_VOLUME_DATA].generation;
    stale_io_counts(opened);
    uint64_t stale_position = 123U;
    assert(openrfsfs_seek(opened, 0, OPENRFSFS_SEEK_START, &stale_position) == OPENRFSFS_STATUS_STALE_HANDLE && stale_position == 0U);
    assert(openrfsfs_set_append(opened, true) == OPENRFSFS_STATUS_STALE_HANDLE);
    assert(openrfsfs_ftruncate(opened, 0U) == OPENRFSFS_STATUS_STALE_HANDLE);
    assert(openrfsfs_unlink_held_file(opened, expected_path) == OPENRFSFS_STATUS_STALE_HANDLE);
    assert(held_unlink_calls == 2U);
    assert(openrfsfs_fsync(opened) == OPENRFSFS_STATUS_STALE_HANDLE && file_sync_calls == 2U);
    assert(openrfsfs_fstat(opened, &metadata) == OPENRFSFS_STATUS_STALE_HANDLE && file_stat_calls == 1U);
    assert(metadata.object_id == 0U);
    assert(openrfsfs_publish_file(opened, expected_path, "other/target") == OPENRFSFS_STATUS_STALE_HANDLE);
    assert(publication_calls == 2U);
    --mounts[OPENRFSFS_VOLUME_DATA].generation;
    mounts[OPENRFSFS_VOLUME_DATA].active = false;
    stale_io_counts(opened);
    mounts[OPENRFSFS_VOLUME_DATA].active = true;
    const size_t held_vnode = open_files[(opened & 0xffU) - 1U].vnode_index;
    ++vnodes[held_vnode].generation;
    stale_io_counts(opened);
    --vnodes[held_vnode].generation;
    backend.write = partial_write;
    size_t partial_bytes = 99U;
    assert(openrfsfs_write(opened, (const uint8_t *)"four", 4U, &partial_bytes) == OPENRFSFS_STATUS_IO);
    assert(partial_bytes == 2U);
    assert(openrfsfs_close(opened) == OPENRFSFS_STATUS_OK);
    stale_io_counts(opened);
    stale_io_counts(0U);
    stale_io_counts(UINT64_MAX);
    assert(openrfsfs_fsync(opened) == OPENRFSFS_STATUS_STALE_HANDLE && file_sync_calls == 2U);
    assert(openrfsfs_fstat(opened, &metadata) == OPENRFSFS_STATUS_STALE_HANDLE && file_stat_calls == 1U);
    assert(mounts[OPENRFSFS_VOLUME_DATA].references == 0U);
    for (size_t index = 0U; index < VFS_MAX_VNODES; ++index) assert(!vnodes[index].active);
    size_t occupied[VFS_MAX_VNODES];
    for (size_t index = 0U; index < VFS_MAX_VNODES; ++index) {
        metadata.object_id = 1000U + index;
        assert(vnode_retain(OPENRFSFS_VOLUME_DATA, "held", &metadata, &occupied[index]) == OPENRFSFS_STATUS_OK);
    }
    const unsigned before_vnode_full = prepared_calls;
    assert(openrfsfs_open_options(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_READ_WRITE,
        expected_prepared_flags, 01720U, &opened) == OPENRFSFS_STATUS_NO_HANDLES);
    assert(opened == 0U && prepared_calls == before_vnode_full && live_backend_handles == 0U);
    vnode_release(occupied[VFS_MAX_VNODES - 1U], vnodes[occupied[VFS_MAX_VNODES - 1U]].generation);
    consume_last_vnode = true;
    for (unsigned failure = 0U; failure < 2U; ++failure) {
        mutation_result = failure == 0U ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
        assert(openrfsfs_open_options(OPENRFSFS_VOLUME_DATA, expected_path, OPENRFSFS_ACCESS_READ_WRITE,
            expected_prepared_flags, 01720U, &opened) == mutation_result);
        if (failure == 0U) assert(opened == 0U);
        else assert(openrfsfs_close(opened) == OPENRFSFS_STATUS_OK);
        for (size_t index = 0U; index < VFS_MAX_VNODES; ++index) assert(!vnode_reservations[index]);
        assert(live_backend_handles == 0U && mounts[OPENRFSFS_VOLUME_DATA].references == VFS_MAX_VNODES - 1U);
    }
    consume_last_vnode = false;
    for (size_t index = 0U; index + 1U < VFS_MAX_VNODES; ++index)
        vnode_release(occupied[index], vnodes[occupied[index]].generation);
    assert(mounts[OPENRFSFS_VOLUME_DATA].references == 0U);
    uint8_t attribute[4];
    size_t attribute_bytes = 99U;
    assert(openrfsfs_get_xattr(OPENRFSFS_VOLUME_DATA, "../invalid", "user.test",
        attribute, sizeof(attribute), &attribute_bytes) == OPENRFSFS_STATUS_PATH);
    assert(attribute_bytes == 0U);
    attribute_bytes = 99U;
    assert(openrfsfs_get_xattr(OPENRFSFS_VOLUME_DATA, "parent/file", "user.test",
        attribute, sizeof(attribute), &attribute_bytes) == OPENRFSFS_STATUS_ACCESS);
    assert(attribute_bytes == 0U);
    backend.get_xattr = read_attribute;
    for (unsigned attempt = 0U; attempt < 2U; ++attempt) {
        mutation_result = attempt == 0U ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
        assert(openrfsfs_get_xattr(OPENRFSFS_VOLUME_DATA, "parent/file", "user.test",
            attribute, sizeof(attribute), &attribute_bytes) == mutation_result);
        assert(attribute_bytes == (attempt == 0U ? 0U : 3U));
    }
    attribute_bytes = 99U;
    assert(openrfsfs_readlink(OPENRFSFS_VOLUME_DATA, "parent/file", NULL, 4U,
        &attribute_bytes) == OPENRFSFS_STATUS_INVALID_ARGUMENT);
    assert(attribute_bytes == 0U);
    nested_open_reservations();
    callback_slot_reuse();
    assert(!openrfsfs_resources_released());
    mounts[OPENRFSFS_VOLUME_DATA].active = false;
    assert(openrfsfs_resources_released());
    vnode_reservations[0] = true;
    assert(!openrfsfs_resources_released());
    vnode_reservations[0] = false;
    open_files[0].opening = true;
    assert(!openrfsfs_resources_released());
    open_files[0].opening = false;
    directories[0].backend_handle = 1U;
    assert(!openrfsfs_resources_released());
    directories[0].backend_handle = 0U;
    assert(openrfsfs_resources_released());
    backend.unmount = reentrant_unmount;
    mounts[OPENRFSFS_VOLUME_DATA].backend = &backend;
    mounts[OPENRFSFS_VOLUME_DATA].active = true;
    const uint64_t unmount_generation = mounts[OPENRFSFS_VOLUME_DATA].generation;
    mutation_result = OPENRFSFS_STATUS_IO;
    for (unsigned attempt = 0U; attempt < 2U; ++attempt) {
        assert(openrfsfs_unmount(OPENRFSFS_VOLUME_DATA) == OPENRFSFS_STATUS_IO);
        assert(mounts[OPENRFSFS_VOLUME_DATA].active && !mounts[OPENRFSFS_VOLUME_DATA].unmounting);
        assert(mounts[OPENRFSFS_VOLUME_DATA].generation == unmount_generation);
        assert(mounts[OPENRFSFS_VOLUME_DATA].backend == &backend && !openrfsfs_resources_released());
    }
    mutation_result = OPENRFSFS_STATUS_OK;
    assert(openrfsfs_unmount(OPENRFSFS_VOLUME_DATA) == OPENRFSFS_STATUS_OK && unmount_calls == 3U);
    assert(openrfsfs_resources_released());
    backend.mount = reentrant_mount;
    backend.drive = mounted_drive;
    volume_backends[OPENRFSFS_VOLUME_DATA] = &backend;
    mutation_result = OPENRFSFS_STATUS_IO;
    const uint64_t next_mount = next_mount_generation;
    for (unsigned attempt = 0U; attempt < 2U; ++attempt) {
        assert(openrfsfs_mount(OPENRFSFS_VOLUME_DATA) == OPENRFSFS_STATUS_IO);
        assert(!mounts[OPENRFSFS_VOLUME_DATA].active && !mounts[OPENRFSFS_VOLUME_DATA].mounting);
        assert(next_mount_generation == next_mount && openrfsfs_resources_released());
    }
    mutation_result = OPENRFSFS_STATUS_OK;
    assert(openrfsfs_mount(OPENRFSFS_VOLUME_DATA) == OPENRFSFS_STATUS_OK && mount_calls == 3U);
    assert(mounts[OPENRFSFS_VOLUME_DATA].active && !mounts[OPENRFSFS_VOLUME_DATA].mounting);
    assert(mounts[OPENRFSFS_VOLUME_DATA].generation == next_mount && !openrfsfs_resources_released());
    assert(openrfsfs_unmount(OPENRFSFS_VOLUME_DATA) == OPENRFSFS_STATUS_OK && unmount_calls == 4U);
    assert(openrfsfs_resources_released());
    size_t absent_count = 99U;
    uint8_t absent_bytes[4];
    openrfsfs_directory_handle absent_directory = 99U;
    struct openrfsfs_stat absent_stat = { .size = 99U };
    assert(openrfsfs_get_xattr(OPENRFSFS_VOLUME_DATA, "file", "user.note", absent_bytes, sizeof(absent_bytes), &absent_count) ==
        OPENRFSFS_STATUS_NOT_MOUNTED && absent_count == 0U);
    absent_count = 99U;
    assert(openrfsfs_readlink(OPENRFSFS_VOLUME_DATA, "file", absent_bytes, sizeof(absent_bytes), &absent_count) ==
        OPENRFSFS_STATUS_NOT_MOUNTED && absent_count == 0U);
    assert(openrfsfs_directory_open(OPENRFSFS_VOLUME_DATA, ".", &absent_directory) == OPENRFSFS_STATUS_NOT_MOUNTED && absent_directory == 0U);
    assert(openrfsfs_lstat_path(OPENRFSFS_VOLUME_DATA, "file", &absent_stat) == OPENRFSFS_STATUS_NOT_MOUNTED && absent_stat.size == 0U);
    assert(openrfsfs_resources_released());
    puts("VFS path mutations pin admission through backend refusal and clear unavailable outputs without leaks: PASS");
    puts("VFS remount reserves admission through backend setup and publishes one successful generation: PASS");
    puts("VFS teardown blocks reentrant admission and retains mount generation across failed release: PASS");
    puts("VFS journal mutation retries, nested open reservations, backend errors, path bounds and vnode census: PASS");
    return 0;
}
