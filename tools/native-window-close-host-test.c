/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <openrfs/cpu.h>
#include <openrfs/native_handle.h>
#include <openrfs/native_process.h>
#include <openrfs/ui.h>

static bool host_interrupts_enabled = true;
static bool host_window_open = true;
static unsigned host_window_close_calls;

bool cpu_interrupts_enabled(void)
{
    return host_interrupts_enabled;
}

void cpu_interrupt_disable(void)
{
    host_interrupts_enabled = false;
}

void cpu_interrupt_enable(void)
{
    host_interrupts_enabled = true;
}

bool ui_native_window_is_open(uint32_t slot)
{
    assert(slot == 3U);
    return host_window_open;
}

enum ui_status ui_native_window_close(uint32_t slot)
{
    assert(slot == 3U);
    ++host_window_close_calls;
    host_window_open = false;
    return UI_STATUS_SURFACE_FAILURE;
}

#include "../src/kernel/native_process.c"

// These unrelated dispatches must never run in the window fixture. Keeping
// aborting stubs makes an accidental routing change a test failure.
enum network_status network_close(uint64_t owner, network_handle handle)
{ (void)owner; (void)handle; assert(false); return 0; }
enum package_control_status package_control_close(uint64_t owner,
    package_control_token token, struct package_control_report *report)
{ (void)owner; (void)token; (void)report; assert(false); return 0; }
enum audio_native_status audio_native_close_report(uint64_t owner,
    uint64_t token, bool *consumed)
{ (void)owner; (void)token; (void)consumed; assert(false); return 0; }
enum openrfsfs_status openrfsfs_close_report(openrfsfs_handle handle, bool *consumed)
{ (void)handle; (void)consumed; assert(false); return 0; }
enum openrfsfs_status openrfsfs_directory_close_report(openrfsfs_directory_handle handle,
    bool *consumed)
{ (void)handle; (void)consumed; assert(false); return 0; }
enum heap_status heap_free(void *pointer)
{ (void)pointer; assert(false); return 0; }
enum package_upload_status package_upload_close_report(uint64_t owner,
    package_upload_token token, struct package_upload_report *report, bool *consumed)
{ (void)owner; (void)token; (void)report; (void)consumed; assert(false); return 0; }
enum paging_status paging_process_unmap_user_page(struct paging_process_space *space,
    enum paging_process_mapping_kind kind, uint64_t address)
{ (void)space; (void)kind; (void)address; assert(false); return 0; }
enum frame_status frame_release(uintptr_t address)
{ (void)address; assert(false); return 0; }

// Keep the production window close branch reachable while unrelated device
// dispatches remain outside this focused host fixture. Assert the table type.
static enum native_resource_close_result close_window(uint8_t type,
    const struct native_resource *resource, void *context)
{
    assert(type == OPENRFS_HANDLE_WINDOW);
    return close_resource(OPENRFS_HANDLE_WINDOW, resource, context);
}

int main(void)
{
    const uint64_t generation = UINT64_C(17);
    const struct native_resource resource = {{3U, generation, 0U, 0U}};
    struct native_process process;
    struct native_handle_table table;
    openrfs_handle_t first;
    openrfs_handle_t duplicate;

    zero_bytes(&process, sizeof(process));
    process.generation = generation;
    process.window.allocated = true;
    process.window.generation = generation;
    process.window.ui_slot = 3U;
    process.window.window_object_open = true;
    process.window.event_object_open = true;

    assert(native_handle_table_initialize(&table, 2U) == NATIVE_HANDLE_OK);
    assert(native_handle_install(&table, OPENRFS_HANDLE_WINDOW, &resource,
        &first) == NATIVE_HANDLE_OK);
    assert(native_handle_duplicate(&table, first, &duplicate) == NATIVE_HANDLE_OK);
    assert(native_handle_close(&table, first, close_window, &process) ==
        NATIVE_HANDLE_OK);
    assert(host_window_close_calls == 0U && table.active_handles == 1U &&
        table.active_objects == 1U);
    assert(native_handle_close(&table, duplicate, close_window, &process) ==
        NATIVE_HANDLE_CLOSE_FAILED);
    assert(host_window_close_calls == 1U && table.active_handles == 0U &&
        table.active_objects == 0U);
    assert(!process.window.window_object_open &&
        process.window.event_object_open);
    assert(native_handle_close_all(&table, close_window, &process) ==
        NATIVE_HANDLE_OK && host_window_close_calls == 1U);
    puts("native window handle close: consumed UI teardown errors retire duplicate wrappers PASS");
    return 0;
}
