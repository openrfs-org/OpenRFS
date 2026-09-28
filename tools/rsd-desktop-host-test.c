/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rsd/fat32_fs.h>
#include <rsd/heap.h>
#include <rsd/package_service.h>
#include <rsd/rsd_desktop.h>
#include <rsd_desktop/files.h>
#include <rsd_desktop/menu.h>
#include <rsd_desktop/packages.h>
#include <rsd_desktop/shell.h>

static uint32_t pixels[1024U * 768U];
static bool renamed_readme;
static bool moved_note;
static bool removed_readme;
static bool removed_docs;
static bool packages_available = true;
static bool copied_note;
static bool copied_folder;
static bool copying_folder_file;
static size_t copied_bytes;
static size_t note_read_offset;
static bool directory_entry_read;
static bool reject_copy_write;
static bool cleaned_failed_copy;
static const uint8_t note_bytes[] = "hello notes\n";

enum rsdfs_status rsdfs_lstat_path(enum rsdfs_volume volume,
    const char *path, struct rsdfs_stat *stat)
{
    if (volume != RSDFS_VOLUME_DATA) {
        return RSDFS_STATUS_NOT_FOUND;
    }
    if (strcmp(path, "/docs") == 0) {
        *stat = (struct rsdfs_stat){ .directory = true,
            .mode = UINT16_C(0755) };
        return RSDFS_STATUS_OK;
    }
    if (strcmp(path, "/notes.txt") != 0 &&
            strcmp(path, "/docs/notes.txt") != 0) {
        return RSDFS_STATUS_NOT_FOUND;
    }
    *stat = (struct rsdfs_stat){ .size = sizeof(note_bytes) - 1U,
        .mode = UINT16_C(0644) };
    return RSDFS_STATUS_OK;
}

enum rsdfs_status rsdfs_open(enum rsdfs_volume volume, const char *path,
    enum rsdfs_access access, rsdfs_handle *handle)
{
    if (volume != RSDFS_VOLUME_DATA ||
            (strcmp(path, "/notes.txt") != 0 &&
                strcmp(path, "/docs/notes.txt") != 0) ||
            access != RSDFS_ACCESS_READ) {
        return RSDFS_STATUS_NOT_FOUND;
    }
    note_read_offset = 0U;
    *handle = UINT64_C(1);
    return RSDFS_STATUS_OK;
}

enum rsdfs_status rsdfs_open_options(enum rsdfs_volume volume,
    const char *path, enum rsdfs_access access, uint8_t flags,
    uint16_t mode, rsdfs_handle *handle)
{
    if (volume != RSDFS_VOLUME_DATA ||
            (strcmp(path, "/notes (copy).txt") != 0 &&
                strcmp(path, "/docs (copy)/notes.txt") != 0) ||
            access != RSDFS_ACCESS_WRITE ||
            flags != (RSDFS_OPEN_CREATE | RSDFS_OPEN_EXCLUSIVE) ||
            mode != UINT16_C(0644)) {
        return RSDFS_STATUS_INVALID_ARGUMENT;
    }
    copied_bytes = 0U;
    copying_folder_file =
        strcmp(path, "/docs (copy)/notes.txt") == 0;
    *handle = UINT64_C(2);
    return RSDFS_STATUS_OK;
}

enum rsdfs_status rsdfs_read(rsdfs_handle handle, uint8_t *destination,
    size_t capacity, size_t *read_bytes)
{
    size_t available = sizeof(note_bytes) - 1U - note_read_offset;
    size_t count = capacity < available ? capacity : available;

    if (handle != UINT64_C(1)) {
        return RSDFS_STATUS_STALE_HANDLE;
    }
    memcpy(destination, note_bytes + note_read_offset, count);
    note_read_offset += count;
    *read_bytes = count;
    return RSDFS_STATUS_OK;
}

enum rsdfs_status rsdfs_write(rsdfs_handle handle, const uint8_t *source,
    size_t source_bytes, size_t *written_bytes)
{
    size_t piece = source_bytes > 4U ? 4U : source_bytes;

    if (reject_copy_write && copied_bytes >= 4U) {
        return RSDFS_STATUS_IO;
    }
    if (handle != UINT64_C(2) ||
            copied_bytes + piece > sizeof(note_bytes) - 1U ||
            memcmp(source, note_bytes + copied_bytes, piece) != 0) {
        return RSDFS_STATUS_IO;
    }
    copied_bytes += piece;
    *written_bytes = piece;
    return RSDFS_STATUS_OK;
}

enum rsdfs_status rsdfs_fsync(rsdfs_handle handle)
{
    return handle == UINT64_C(2) &&
        copied_bytes == sizeof(note_bytes) - 1U ?
        RSDFS_STATUS_OK : RSDFS_STATUS_IO;
}

enum rsdfs_status rsdfs_close(rsdfs_handle handle)
{
    return handle == UINT64_C(1) || handle == UINT64_C(2) ?
        RSDFS_STATUS_OK : RSDFS_STATUS_STALE_HANDLE;
}

enum rsdfs_status rsdfs_sync(enum rsdfs_volume volume)
{
    if (volume != RSDFS_VOLUME_DATA ||
            copied_bytes != sizeof(note_bytes) - 1U) {
        return RSDFS_STATUS_IO;
    }
    if (copying_folder_file) {
        copied_folder = true;
    } else {
        copied_note = true;
    }
    return RSDFS_STATUS_OK;
}

enum rsdfs_status rsdfs_directory_open(enum rsdfs_volume volume,
    const char *path, rsdfs_directory_handle *handle)
{
    if (volume != RSDFS_VOLUME_DATA || strcmp(path, "/docs") != 0) {
        return RSDFS_STATUS_NOT_DIRECTORY;
    }
    directory_entry_read = false;
    *handle = UINT64_C(3);
    return RSDFS_STATUS_OK;
}

enum rsdfs_status rsdfs_directory_read(rsdfs_directory_handle handle,
    struct rsdfs_list_entry *entry, bool *present)
{
    if (handle != UINT64_C(3)) {
        return RSDFS_STATUS_STALE_HANDLE;
    }
    *present = !directory_entry_read;
    if (*present) {
        *entry = (struct rsdfs_list_entry){ .name = "notes.txt",
            .size = sizeof(note_bytes) - 1U };
        directory_entry_read = true;
    }
    return RSDFS_STATUS_OK;
}

enum rsdfs_status rsdfs_directory_close(rsdfs_directory_handle handle)
{
    return handle == UINT64_C(3) ? RSDFS_STATUS_OK :
        RSDFS_STATUS_STALE_HANDLE;
}

enum rsdfs_status rsdfs_mkdir_mode(enum rsdfs_volume volume,
    const char *path, uint16_t mode)
{
    return volume == RSDFS_VOLUME_DATA &&
        strcmp(path, "/docs (copy)") == 0 &&
        mode == UINT16_C(0755) ? RSDFS_STATUS_OK :
        RSDFS_STATUS_NOT_DIRECTORY;
}

enum heap_status heap_allocate(uint64_t size, void **pointer)
{
    *pointer = malloc((size_t)size);
    return *pointer == NULL ? HEAP_STATUS_OUT_OF_MEMORY : HEAP_STATUS_OK;
}

enum heap_status heap_free(void *pointer)
{
    free(pointer);
    return HEAP_STATUS_OK;
}

enum package_service_status package_service_snapshot(uint8_t *database,
    size_t capacity, size_t *output_bytes, struct package_service_report *report)
{
    (void)report;
    *output_bytes = 0U;
    if (!packages_available || capacity == 0U) {
        return PACKAGE_SERVICE_STATUS_STATE;
    }
    database[0] = 1U;
    *output_bytes = 1U;
    return PACKAGE_SERVICE_STATUS_OK;
}

enum package_state_status package_state_database_parse(const uint8_t *bytes,
    size_t byte_count, struct package_state_database_view *result)
{
    if (byte_count != 1U || bytes[0] != 1U) {
        return PACKAGE_STATE_STATUS_MAGIC;
    }
    *result = (struct package_state_database_view){ .bytes = bytes,
        .byte_count = byte_count, .package_count = 2U };
    return PACKAGE_STATE_STATUS_OK;
}

enum package_state_status package_state_database_package(
    const struct package_state_database_view *database, uint32_t index,
    struct package_state_package_view *result)
{
    static const uint8_t first[] = "rsd-files";
    static const uint8_t second[] = "rsd-shell";
    static const uint8_t version[] = "1.0";
    const uint8_t *name = index == 0U ? first : second;

    if (database->package_count != 2U || index >= 2U) {
        return PACKAGE_STATE_STATUS_PACKAGE;
    }
    *result = (struct package_state_package_view){
        .identifier = { name, index == 0U ? sizeof(first) - 1U :
            sizeof(second) - 1U },
        .version = { version, sizeof(version) - 1U }
    };
    return PACKAGE_STATE_STATUS_OK;
}

enum rsdfs_status rsdfs_list(enum rsdfs_volume volume, const char *path,
    struct rsdfs_list_entry *entries, size_t capacity, size_t *count)
{
    if (volume != RSDFS_VOLUME_DATA || entries == NULL || count == NULL) {
        return RSDFS_STATUS_INVALID_ARGUMENT;
    }
    if (strcmp(path, "/") == 0 && capacity >= 2U) {
        entries[0] = (struct rsdfs_list_entry){ .name = "docs",
            .directory = true };
        entries[1] = (struct rsdfs_list_entry){ .name = "readme.txt",
            .size = 42U };
        *count = 2U;
        return RSDFS_STATUS_OK;
    }
    if (strcmp(path, "/docs") == 0 && capacity >= 1U) {
        entries[0] = (struct rsdfs_list_entry){ .name = "notes.txt",
            .size = 12U };
        *count = 1U;
        return RSDFS_STATUS_OK;
    }
    return RSDFS_STATUS_NOT_FOUND;
}

enum rsdfs_status rsdfs_rename(enum rsdfs_volume volume, const char *from,
    const char *to)
{
    if (volume != RSDFS_VOLUME_DATA) {
        return RSDFS_STATUS_INVALID_ARGUMENT;
    }
    if (strcmp(from, "/readme.txt") == 0 &&
            strcmp(to, "/changed.txt") == 0) {
        renamed_readme = true;
        return RSDFS_STATUS_OK;
    }
    if (strcmp(from, "/docs/notes.txt") == 0 &&
            strcmp(to, "/notes.txt") == 0) {
        moved_note = true;
        return RSDFS_STATUS_OK;
    }
    return RSDFS_STATUS_IO;
}

enum rsdfs_status rsdfs_unlink(enum rsdfs_volume volume, const char *path)
{
    if (volume == RSDFS_VOLUME_DATA &&
            strcmp(path, "/notes (copy).txt") == 0) {
        cleaned_failed_copy = true;
        return RSDFS_STATUS_OK;
    }
    if (volume == RSDFS_VOLUME_DATA && renamed_readme &&
            strcmp(path, "/changed.txt") == 0) {
        removed_readme = true;
        return RSDFS_STATUS_OK;
    }
    return RSDFS_STATUS_IO;
}

enum rsdfs_status rsdfs_rmdir(enum rsdfs_volume volume, const char *path)
{
    if (volume == RSDFS_VOLUME_DATA && moved_note &&
            strcmp(path, "/docs") == 0) {
        removed_docs = true;
        return RSDFS_STATUS_OK;
    }
    return RSDFS_STATUS_IO;
}

int main(void)
{
    struct ui_rect terminal;
    struct rsd_rect menu;
    struct ui_event close = { .type = UI_EVENT_PANEL_CLOSE };
    struct ui_event press = {
        .type = UI_EVENT_POINTER_BUTTON_PRESS,
        .point = { 20, 20 },
        .button = UI_POINTER_BUTTON_LEFT
    };
    struct ui_event move = {
        .type = UI_EVENT_POINTER_MOVEMENT,
        .point = { 700, 600 }
    };

    if (rsd_desktop_construct(pixels, 799U, 768U) ||
            !rsd_desktop_self_test() ||
            !rsd_desktop_construct(pixels, 1024U, 768U) ||
            !rsd_desktop_terminal_client(&terminal) ||
            rsd_menu_row_count() != 7U ||
            rsd_shell_run_match_count() != 5U ||
            terminal.x != 84U || terminal.y != 108U ||
            terminal.width != 552U || terminal.height != 308U) {
        fputs("RSD desktop construction or terminal geometry failed\n",
            stderr);
        return 1;
    }
    if (!rsd_packages_live() || rsd_packages_count() != 2U ||
            !rsd_packages_installed("rsd-files") ||
            rsd_packages_marked() != 0U || rsd_packages_apply() != 0U) {
        fputs("RSD Packages did not read installed state\n", stderr);
        return 1;
    }
    rsd_packages_mark(0U, RSD_PACKAGE_REMOVE);
    if (rsd_packages_marked() != 0U) {
        fputs("RSD Packages allowed a fake package change\n", stderr);
        return 1;
    }
    packages_available = false;
    if (rsd_packages_refresh() || rsd_packages_count() != 0U) {
        fputs("RSD Packages retained stale state after failure\n", stderr);
        return 1;
    }
    packages_available = true;
    if (!rsd_packages_refresh() || rsd_packages_count() != 2U) {
        fputs("RSD Packages reload failed\n", stderr);
        return 1;
    }
    if (rsd_files_child_count(rsd_files_root()) != 2U ||
            !rsd_files_refresh() ||
            rsd_files_child_count(rsd_files_root()) != 2U ||
            strcmp(rsd_files_node_name(1U), "docs") != 0 ||
            !rsd_files_open(1U) || rsd_files_child_count(1U) != 1U ||
            strcmp(rsd_files_node_name(3U), "notes.txt") != 0 ||
            rsd_files_rename(3U, "denied.txt") ||
            strcmp(rsd_files_node_name(3U), "notes.txt") != 0 ||
            !rsd_files_rename(2U, "changed.txt") || !renamed_readme ||
            !rsd_files_remove(2U) || !removed_readme) {
        fputs("RSD Files did not use the mounted Data backend\n", stderr);
        return 1;
    }
    rsd_files_select(1U, false);
    if (!rsd_files_copy_selection(false) ||
            rsd_files_paste_into(rsd_files_root()) != 1U ||
            !copied_folder ||
            strcmp(rsd_files_node_name(4U), "docs (copy)") != 0) {
        fputs("RSD Files did not recursively copy a Data folder\n", stderr);
        return 1;
    }
    rsd_files_select(3U, false);
    if (!rsd_files_copy_selection(true) ||
            rsd_files_paste_into(1U) != 0U ||
            !rsd_files_clipboard_is_cut() ||
            rsd_files_paste_into(rsd_files_root()) != 1U || !moved_note ||
            !rsd_files_open(rsd_files_root()) ||
            !rsd_files_remove(1U) || !removed_docs ||
            rsd_files_paste_into(rsd_files_root()) != 0U) {
        fputs("RSD Files did not use the mounted Data backend\n", stderr);
        return 1;
    }
    rsd_files_select(3U, false);
    reject_copy_write = true;
    if (!rsd_files_copy_selection(false) ||
            rsd_files_paste_into(rsd_files_root()) != 0U ||
            !cleaned_failed_copy ||
            rsd_files_child_count(rsd_files_root()) != 2U) {
        fputs("RSD Files retained a failed Data copy\n", stderr);
        return 1;
    }
    reject_copy_write = false;
    if (!rsd_files_clipboard_has() ||
            rsd_files_paste_into(rsd_files_root()) != 1U ||
            !copied_note || copied_bytes != sizeof(note_bytes) - 1U ||
            strcmp(rsd_files_node_name(5U), "notes (copy).txt") != 0) {
        fputs("RSD Files did not copy file bytes through Data\n", stderr);
        return 1;
    }
    rsd_desktop_draw();
    if (pixels[80U * 1024U + 80U] ==
            pixels[40U * 1024U + 40U]) {
        fputs("RSD desktop frame did not reach the surface\n", stderr);
        return 1;
    }
    if (rsd_shell_open(RSD_APP_TERMINAL,
            (struct rsd_rect){ 400U, 400U, 300U, 200U }) >=
                RSD_SHELL_MAX_WINDOWS ||
            rsd_shell_window_count() != 2U ||
            !rsd_desktop_terminal_client(&terminal) || terminal.x < 400U) {
        fputs("second RSD terminal did not open\n",
            stderr);
        return 1;
    }
    (void)rsd_desktop_event(&move);
    if (!rsd_desktop_terminal_client(&terminal) || terminal.x < 400U) {
        fputs("pointer hover moved the terminal without a drag\n", stderr);
        return 1;
    }
    if (!rsd_desktop_event(&close) ||
            rsd_shell_window_count() != 1U ||
            !rsd_desktop_terminal_client(&terminal) ||
            !rsd_desktop_event(&close) ||
            rsd_shell_window_count() != 0U ||
            !rsd_desktop_event(&press) ||
            !rsd_shell_root_menu_open() ||
            !rsd_shell_root_menu_bounds(&menu)) {
        fputs("RSD desktop close or root menu failed\n", stderr);
        return 1;
    }
    press.point.x = (int32_t)(menu.x + 20U);
    press.point.y = (int32_t)(menu.y + RSD_MENU_TITLE_HEIGHT + 10U);
    if (!rsd_desktop_event(&press) || rsd_shell_root_menu_open() ||
            !rsd_desktop_terminal_client(&terminal)) {
        fputs("RSD desktop terminal relaunch failed\n", stderr);
        return 1;
    }
    rsd_packages_reset();
    for (uint32_t at = 0U; at < 40U; ++at) {
        if (!rsd_packages_add("package", "1.0", "", true)) {
            fputs("RSD Packages could not hold installed entries\n", stderr);
            return 1;
        }
    }
    const struct rsd_window *window = rsd_shell_window(rsd_shell_focused());
    if (window == NULL || !rsd_packages_turn_page(window, true) ||
            rsd_packages_first_visible(window) == 0U ||
            !rsd_packages_turn_page(window, false) ||
            rsd_packages_first_visible(window) != 0U) {
        fputs("RSD Packages pagination failed\n", stderr);
        return 1;
    }
    puts("RSD desktop host test passed");
    return 0;
}
