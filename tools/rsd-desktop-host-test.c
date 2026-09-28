/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <rsd/fat32_fs.h>
#include <rsd/rsd_desktop.h>
#include <rsd_desktop/files.h>
#include <rsd_desktop/menu.h>
#include <rsd_desktop/shell.h>

static uint32_t pixels[1024U * 768U];
static bool renamed_readme;
static bool moved_note;
static bool removed_readme;
static bool removed_docs;

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
    puts("RSD desktop host test passed");
    return 0;
}
