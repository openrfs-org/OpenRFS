/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <openrfs/native_handle.h>
#include "../src/kernel/vfs.c"

static bool host_interrupts_enabled = true;
bool cpu_interrupts_enabled(void) { return host_interrupts_enabled; }
void cpu_interrupt_disable(void) { host_interrupts_enabled = false; }
void cpu_interrupt_enable(void) { host_interrupts_enabled = true; }

static unsigned calls;
static enum native_resource_close_result next_result;
static enum native_resource_close_result close_file(uint8_t type,
    const struct native_resource *resource, void *context)
{
    assert(type == OPENRFS_HANDLE_FILE && resource->words[0] == 77U && context == &calls);
    ++calls;
    return next_result;
}

struct close_script {
    enum native_resource_close_result file_result;
    enum native_resource_close_result directory_result;
    unsigned file_calls;
    unsigned directory_calls;
};

static enum native_resource_close_result close_scripted(uint8_t type,
    const struct native_resource *resource, void *context)
{
    struct close_script *script = context;

    assert(resource != NULL && script != NULL);
    if (type == OPENRFS_HANDLE_FILE) {
        assert(resource->words[0] == 77U);
        ++script->file_calls;
        return script->file_result;
    }
    assert(type == OPENRFS_HANDLE_DIRECTORY && resource->words[0] == 88U);
    ++script->directory_calls;
    return script->directory_result;
}

static void assert_table_empty(const struct native_handle_table *table)
{
    assert(table->active_handles == 0U && table->active_objects == 0U);
}

static void mixed_close_all_test(void)
{
    const struct native_resource file = {{77U, 0U, 0U, 0U}};
    const struct native_resource directory = {{88U, 0U, 0U, 0U}};
    struct close_script script = {
        NATIVE_RESOURCE_RETAINED,
        NATIVE_RESOURCE_CLOSED_WITH_ERROR,
        0U,
        0U,
    };
    struct native_handle_table table;
    openrfs_handle_t file_handle;
    openrfs_handle_t file_duplicate;
    openrfs_handle_t directory_handle;
    struct native_resource *resolved;
    struct native_handle_close_report report;

    assert(native_handle_table_initialize(&table, 3U) == NATIVE_HANDLE_OK);
    assert(native_handle_install(&table, OPENRFS_HANDLE_FILE, &file,
        &file_handle) == NATIVE_HANDLE_OK);
    assert(native_handle_duplicate(&table, file_handle, &file_duplicate) ==
        NATIVE_HANDLE_OK);
    assert(native_handle_install(&table, OPENRFS_HANDLE_DIRECTORY, &directory,
        &directory_handle) == NATIVE_HANDLE_OK);

    /* close_all must retire the consumed directory error and still retain the
     * final duplicate's file wrapper when its callback refuses first. */
    assert(native_handle_close_all_diagnostics(&table, close_scripted, &script,
        &report) == NATIVE_HANDLE_CLOSE_FAILED);
    assert(report.attempted_handles == 3U && report.callback_attempts == 2U &&
        report.duplicate_references == 1U && report.retained_resources == 1U &&
        report.consumed_error_resources == 1U && report.retired_handles == 2U &&
        report.active_handles_after == 1U && report.retryable && report.progress &&
        script.file_calls == 1U &&
        script.directory_calls == 1U);
    assert(table.active_handles == 1U && table.active_objects == 1U);
    assert(native_handle_resolve(&table, file_duplicate, OPENRFS_HANDLE_FILE,
        &resolved) == NATIVE_HANDLE_OK && resolved->words[0] == 77U);

    script.file_result = NATIVE_RESOURCE_CLOSED;
    assert(native_handle_close_all_diagnostics(&table, close_scripted, &script,
        &report) == NATIVE_HANDLE_OK);
    assert(report.attempted_handles == 1U && report.closed_resources == 1U &&
        !report.retryable && report.progress && script.file_calls == 2U &&
        script.directory_calls == 1U);
    assert_table_empty(&table);
}

static void close_all_report_argument_test(void)
{
    struct native_handle_table table = {0};
    bool retryable = true;
    struct native_handle_close_report report;

    assert(native_handle_close_all_report(NULL, close_file, &calls,
        &retryable) == NATIVE_HANDLE_NULL_ARGUMENT && !retryable);
    assert(native_handle_close_all_report(&table, close_file, &calls, NULL) ==
        NATIVE_HANDLE_NULL_ARGUMENT);
    assert(native_handle_close_all_report(&table, close_file, &calls,
        &retryable) == NATIVE_HANDLE_BAD_LIMIT && !retryable);
    assert(native_handle_close_all_diagnostics(&table, close_file, &calls,
        &report) == NATIVE_HANDLE_BAD_LIMIT && report.invalid_arguments == 1U &&
        report.status == NATIVE_HANDLE_BAD_LIMIT);
}

static void duplicate_reference_census_test(void)
{
    const struct native_resource resource = {{77U, 0U, 0U, 0U}};
    struct close_script script = {
        NATIVE_RESOURCE_RETAINED,
        NATIVE_RESOURCE_CLOSED,
        0U,
        0U,
    };
    struct native_handle_table table;
    openrfs_handle_t first;
    openrfs_handle_t second;
    openrfs_handle_t third;
    struct native_resource *resolved;
    struct native_handle_close_report report;

    assert(native_handle_table_initialize(&table, 3U) == NATIVE_HANDLE_OK);
    assert(native_handle_install(&table, OPENRFS_HANDLE_FILE, &resource,
        &first) == NATIVE_HANDLE_OK);
    assert(native_handle_duplicate(&table, first, &second) ==
        NATIVE_HANDLE_OK);
    assert(native_handle_duplicate(&table, first, &third) ==
        NATIVE_HANDLE_OK);
    assert(native_handle_close_all_diagnostics(&table, close_scripted, &script,
        &report) == NATIVE_HANDLE_CLOSE_FAILED);
    assert(report.attempted_handles == 3U && report.callback_attempts == 1U &&
        report.duplicate_references == 2U && report.retained_resources == 1U &&
        report.retired_handles == 2U && report.retryable && report.progress &&
        script.file_calls == 1U &&
        table.active_handles == 1U && table.active_objects == 1U);
    assert(native_handle_resolve(&table, first, OPENRFS_HANDLE_FILE,
        &resolved) == NATIVE_HANDLE_STALE);
    assert(native_handle_resolve(&table, second, OPENRFS_HANDLE_FILE,
        &resolved) == NATIVE_HANDLE_STALE);
    assert(native_handle_resolve(&table, third, OPENRFS_HANDLE_FILE,
        &resolved) == NATIVE_HANDLE_OK);

    script.file_result = NATIVE_RESOURCE_CLOSED;
    assert(native_handle_close_all_diagnostics(&table, close_scripted, &script,
        &report) == NATIVE_HANDLE_OK && !report.retryable);
    assert(report.attempted_handles == 1U && report.closed_resources == 1U &&
        report.active_handles_after == 0U && script.file_calls == 2U);
    assert_table_empty(&table);
    assert(native_handle_close_all_diagnostics(&table, close_scripted, &script,
        &report) == NATIVE_HANDLE_OK && report.attempted_handles == 0U &&
        !report.retryable && !report.progress);
    assert(script.file_calls == 2U);
}

static unsigned directory_close_calls;
static enum openrfsfs_status directory_close_status;

static enum openrfsfs_status directory_stat(enum openrfsfs_volume volume,
    const char *path, struct openrfsfs_stat *stat)
{
    assert(volume == OPENRFSFS_VOLUME_DATA && path != NULL && stat != NULL);
    assert(host_interrupts_enabled);
    *stat = (struct openrfsfs_stat){.object_id = 101U, .directory = true};
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status directory_open(enum openrfsfs_volume volume,
    const char *path, openrfsfs_handle *handle)
{
    assert(volume == OPENRFSFS_VOLUME_DATA && path != NULL && handle != NULL);
    assert(host_interrupts_enabled);
    *handle = 91U;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status directory_read(openrfsfs_handle handle,
    struct openrfsfs_list_entry *entry, bool *present)
{
    assert(handle == 91U && entry != NULL && present != NULL);
    *present = false;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status directory_close(openrfsfs_handle handle)
{
    assert(handle == 91U);
    ++directory_close_calls;
    return directory_close_status;
}

struct native_directory_state {
    openrfsfs_directory_handle iterator;
    bool active;
};

static unsigned native_directory_callback_calls;

static enum native_resource_close_result close_directory(uint8_t type,
    const struct native_resource *resource, void *context)
{
    struct native_directory_state *state = context;
    ++native_directory_callback_calls;
    bool consumed = false;
    const enum openrfsfs_status status = openrfsfs_directory_close_report(
        state->iterator, &consumed);

    assert(type == OPENRFS_HANDLE_DIRECTORY && resource->words[0] == 0U);
    if (consumed) state->active = false;
    return status == OPENRFSFS_STATUS_OK ? NATIVE_RESOURCE_CLOSED :
        consumed ? NATIVE_RESOURCE_CLOSED_WITH_ERROR : NATIVE_RESOURCE_RETAINED;
}

static const struct vfs_backend_ops directory_backend = {
    .stat_path = directory_stat,
    .directory_open = directory_open,
    .directory_read = directory_read,
    .directory_close = directory_close,
    .case_sensitive = true,
};

static void prepare_directory_mount(void)
{
    mounts[OPENRFSFS_VOLUME_DATA].active = true;
    mounts[OPENRFSFS_VOLUME_DATA].mounting = false;
    mounts[OPENRFSFS_VOLUME_DATA].unmounting = false;
    mounts[OPENRFSFS_VOLUME_DATA].generation = 1U;
    mounts[OPENRFSFS_VOLUME_DATA].references = 0U;
    mounts[OPENRFSFS_VOLUME_DATA].backend = &directory_backend;
    directory_close_calls = 0U;
    native_directory_callback_calls = 0U;
    directory_close_status = OPENRFSFS_STATUS_OK;
    for (size_t index = 0U; index < VFS_VNODE_BUCKETS; ++index) {
        vnode_buckets[index] = VFS_NO_INDEX;
    }
}

static openrfsfs_directory_handle open_test_directory(void)
{
    openrfsfs_directory_handle iterator;

    assert(openrfsfs_directory_open(OPENRFSFS_VOLUME_DATA, "root", &iterator) ==
        OPENRFSFS_STATUS_OK);
    return iterator;
}

static void release_directory_mount(void)
{
    mounts[OPENRFSFS_VOLUME_DATA].active = false;
    mounts[OPENRFSFS_VOLUME_DATA].backend = NULL;
    mounts[OPENRFSFS_VOLUME_DATA].references = 0U;
    assert(vnode_resources_released());
    assert(openrfsfs_resources_released());
}

static void assert_directory_wrapper(const struct native_handle_table *table,
    const struct native_directory_state *state, unsigned handles,
    unsigned objects, bool active)
{
    assert(table->active_handles == handles);
    assert(table->active_objects == objects);
    assert(state->active == active);
}

static void retryable_process_teardown_test(void)
{
    const struct native_resource resource = {{0U, 0U, 0U, 0U}};
    struct native_handle_table table;
    struct native_directory_state state = {open_test_directory(), true};
    openrfs_handle_t first;
    openrfs_handle_t duplicate;
    struct native_handle_close_report report;

    assert(native_handle_table_initialize(&table, 2U) == NATIVE_HANDLE_OK);
    assert(native_handle_install(&table, OPENRFS_HANDLE_DIRECTORY, &resource,
        &first) == NATIVE_HANDLE_OK);
    assert(native_handle_duplicate(&table, first, &duplicate) ==
        NATIVE_HANDLE_OK);

    /* A full mount reference count makes the VFS refuse before consuming the
     * iterator.  close_all must leave its final wrapper for a later retry. */
    mounts[OPENRFSFS_VOLUME_DATA].references = SIZE_MAX;
    assert(native_handle_close_all_diagnostics(&table, close_directory,
        &state, &report) == NATIVE_HANDLE_CLOSE_FAILED);
    // The native callback ran and received the VFS refusal; the backend close
    // callback did not run. Measure both layers instead of conflating them.
    assert(native_directory_callback_calls == 1U);
    assert(report.attempted_handles == 2U &&
        report.callback_attempts == native_directory_callback_calls &&
        report.duplicate_references == 1U && report.retained_resources == 1U &&
        report.retired_handles == 1U && report.retryable && report.progress);
    assert(directory_close_calls == 0U);
    assert_directory_wrapper(&table, &state, 1U, 1U, true);
    assert(openrfsfs_directory_read(state.iterator,
        &(struct openrfsfs_list_entry){0}, &(bool){false}) == OPENRFSFS_STATUS_BUSY);

    mounts[OPENRFSFS_VOLUME_DATA].references = 0U;
    assert(native_handle_close_all_diagnostics(&table, close_directory, &state,
        &report) == NATIVE_HANDLE_OK && !report.retryable);
    assert(report.attempted_handles == 1U && report.callback_attempts == 1U &&
        report.closed_resources == 1U && report.active_handles_after == 0U &&
        report.progress);
    assert(directory_close_calls == 1U);
    assert_directory_wrapper(&table, &state, 0U, 0U, false);
    bool consumed = true;
    assert(openrfsfs_directory_close_report(state.iterator, &consumed) ==
        OPENRFSFS_STATUS_STALE_HANDLE && !consumed);
}

static void consumed_error_process_teardown_test(void)
{
    const struct native_resource resource = {{0U, 0U, 0U, 0U}};
    struct native_handle_table table;
    struct native_directory_state state = {open_test_directory(), true};
    openrfs_handle_t first;
    openrfs_handle_t duplicate;
    struct native_handle_close_report report;

    assert(native_handle_table_initialize(&table, 2U) == NATIVE_HANDLE_OK);
    assert(native_handle_install(&table, OPENRFS_HANDLE_DIRECTORY, &resource,
        &first) == NATIVE_HANDLE_OK);
    assert(native_handle_duplicate(&table, first, &duplicate) ==
        NATIVE_HANDLE_OK);
    directory_close_status = OPENRFSFS_STATUS_IO;
    assert(native_handle_close_all_diagnostics(&table, close_directory, &state,
        &report) == NATIVE_HANDLE_CLOSE_FAILED);
    assert(!report.retryable && report.attempted_handles == 2U &&
        report.callback_attempts == 1U &&
        report.consumed_error_resources == 1U && report.duplicate_references == 1U &&
        report.retired_handles == 2U && report.active_handles_after == 0U &&
        report.progress);
    assert(directory_close_calls == 1U);
    assert_directory_wrapper(&table, &state, 0U, 0U, false);
    bool consumed = true;
    assert(openrfsfs_directory_close_report(state.iterator, &consumed) ==
        OPENRFSFS_STATUS_STALE_HANDLE && !consumed);
}

int main(void)
{
    struct native_handle_table table;
    const struct native_resource resource = {{77U, 0U, 0U, 0U}};
    struct native_resource *resolved;
    openrfs_handle_t first, duplicate, reused;
    assert(native_handle_table_initialize(&table, 2U) == NATIVE_HANDLE_OK);
    assert(native_handle_install(&table, OPENRFS_HANDLE_FILE, &resource, &first) == NATIVE_HANDLE_OK);
    assert(native_handle_duplicate(&table, first, &duplicate) == NATIVE_HANDLE_OK);
    next_result = NATIVE_RESOURCE_CLOSED_WITH_ERROR;
    assert(native_handle_close(&table, first, close_file, &calls) == NATIVE_HANDLE_OK);
    assert(calls == 0U && table.active_handles == 1U && table.active_objects == 1U);
    // The VFS identity is already consumed. Return its error and retire the
    // native wrapper; neither close nor close_all may call the backend again.
    assert(native_handle_close(&table, duplicate, close_file, &calls) == NATIVE_HANDLE_CLOSE_FAILED);
    assert(calls == 1U && table.active_handles == 0U && table.active_objects == 0U);
    assert(native_handle_resolve(&table, duplicate, OPENRFS_HANDLE_FILE, &resolved) == NATIVE_HANDLE_STALE);
    assert(native_handle_close_all(&table, close_file, &calls) == NATIVE_HANDLE_OK && calls == 1U);
    assert(native_handle_install(&table, OPENRFS_HANDLE_FILE, &resource, &reused) == NATIVE_HANDLE_OK);
    assert(reused != first && reused != duplicate);
    // A refusal before consumption retains exactly one retryable reference.
    next_result = NATIVE_RESOURCE_RETAINED;
    assert(native_handle_close(&table, reused, close_file, &calls) == NATIVE_HANDLE_CLOSE_FAILED);
    assert(table.active_handles == 1U && table.active_objects == 1U);
    assert(native_handle_resolve(&table, reused, OPENRFS_HANDLE_FILE, &resolved) == NATIVE_HANDLE_OK);
    next_result = NATIVE_RESOURCE_CLOSED;
    assert(native_handle_close(&table, reused, close_file, &calls) == NATIVE_HANDLE_OK);
    assert(calls == 3U && table.active_handles == 0U && table.active_objects == 0U);
    assert(native_handle_install(&table, OPENRFS_HANDLE_FILE, &resource, &first) == NATIVE_HANDLE_OK);
    assert(native_handle_install(&table, OPENRFS_HANDLE_FILE, &resource, &duplicate) == NATIVE_HANDLE_OK);
    next_result = NATIVE_RESOURCE_CLOSED_WITH_ERROR;
    assert(native_handle_close_all(&table, close_file, &calls) == NATIVE_HANDLE_CLOSE_FAILED);
    assert(calls == 5U && table.active_handles == 0U && table.active_objects == 0U);
    assert(native_handle_close_all(&table, close_file, &calls) == NATIVE_HANDLE_OK && calls == 5U);
    mixed_close_all_test();
    close_all_report_argument_test();
    duplicate_reference_census_test();

    prepare_directory_mount();
    const openrfsfs_directory_handle iterator = open_test_directory();
    struct native_directory_state directory_state = {iterator, true};
    struct native_handle_table directory_table;
    const struct native_resource directory_resource = {{0U, 0U, 0U, 0U}};
    openrfs_handle_t directory_handle, directory_duplicate;
    assert(native_handle_table_initialize(&directory_table, 2U) == NATIVE_HANDLE_OK);
    assert(native_handle_install(&directory_table, OPENRFS_HANDLE_DIRECTORY,
        &directory_resource, &directory_handle) == NATIVE_HANDLE_OK);
    assert(native_handle_duplicate(&directory_table, directory_handle,
        &directory_duplicate) == NATIVE_HANDLE_OK);
    directory_close_status = OPENRFSFS_STATUS_IO;
    assert(native_handle_close(&directory_table, directory_handle,
        close_directory, &directory_state) == NATIVE_HANDLE_OK);
    assert(directory_close_calls == 0U && directory_state.active &&
        directory_table.active_handles == 1U);
    assert(native_handle_close(&directory_table, directory_duplicate,
        close_directory, &directory_state) == NATIVE_HANDLE_CLOSE_FAILED);
    assert(directory_close_calls == 1U && !directory_state.active &&
        directory_table.active_handles == 0U && directory_table.active_objects == 0U);
    bool consumed = true;
    assert(openrfsfs_directory_close_report(iterator, &consumed) ==
        OPENRFSFS_STATUS_STALE_HANDLE && !consumed);
    assert(native_handle_close_all(&directory_table, close_directory,
        &directory_state) == NATIVE_HANDLE_OK && directory_close_calls == 1U);
    release_directory_mount();

    prepare_directory_mount();
    retryable_process_teardown_test();
    release_directory_mount();

    prepare_directory_mount();
    consumed_error_process_teardown_test();
    release_directory_mount();
    puts("native handle close: consumed I/O errors retire wrappers, pre-consumption refusals retry, duplicate references and shutdown census PASS");
    return 0;
}
