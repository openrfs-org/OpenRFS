/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>

#include <openrfs/minimal_de.h>
#include <trait/files.h>
#include <trait/menu.h>
#include <trait/shell.h>

static uint32_t pixels[1024U * 768U];

int main(void)
{
    struct ui_rect terminal;
    struct trait_rect menu;
    struct ui_event close = { .type = UI_EVENT_PANEL_CLOSE };
    struct ui_event press = {
        .type = UI_EVENT_POINTER_BUTTON_PRESS,
        .point = { 20, 20 },
        .button = UI_POINTER_BUTTON_RIGHT
    };
    struct ui_event move = {
        .type = UI_EVENT_POINTER_MOVEMENT,
        .point = { 700, 600 }
    };

    if (minimal_de_construct(pixels, 799U, 768U) ||
            !minimal_de_self_test() ||
            !minimal_de_construct(pixels, 1024U, 768U) ||
            !minimal_de_terminal_client(&terminal) ||
            trait_menu_row_count() != 4U ||
            trait_shell_run_match_count() != 3U ||
            terminal.x != 87U || terminal.y != 107U ||
            terminal.width != 546U || terminal.height != 306U) {
        fputs("minimal desktop construction or terminal geometry failed\n",
            stderr);
        return 1;
    }
    minimal_de_draw();
    if (pixels[80U * 1024U + 80U] ==
            pixels[40U * 1024U + 40U]) {
        fputs("minimal desktop frame did not reach the surface\n", stderr);
        return 1;
    }
    if (trait_shell_open(TRAIT_APP_TERMINAL,
            (struct trait_rect){ 400U, 400U, 300U, 200U }) >=
                TRAIT_SHELL_MAX_WINDOWS ||
            trait_shell_window_count() != 1U ||
            !minimal_de_terminal_client(&terminal) || terminal.x != 87U) {
        fputs("reopening the real terminal created a false second view\n",
            stderr);
        return 1;
    }
    (void)minimal_de_event(&move);
    if (!minimal_de_terminal_client(&terminal) || terminal.x != 87U ||
            terminal.y != 107U) {
        fputs("pointer hover moved the terminal without a drag\n", stderr);
        return 1;
    }
    if (!minimal_de_event(&close) ||
            minimal_de_terminal_client(&terminal) ||
            trait_shell_window_count() != 0U ||
            !minimal_de_event(&press) ||
            !trait_shell_root_menu_open() ||
            !trait_shell_root_menu_bounds(&menu)) {
        fputs("minimal desktop close or root menu failed\n", stderr);
        return 1;
    }
    press.point.x = (int32_t)(menu.x + 20U);
    press.point.y = (int32_t)(menu.y + TRAIT_MENU_TITLE_HEIGHT + 10U);
    if (!minimal_de_event(&press) || trait_shell_root_menu_open() ||
            !minimal_de_terminal_client(&terminal)) {
        fputs("minimal desktop terminal relaunch failed\n", stderr);
        return 1;
    }
    if (!minimal_de_event(&close) || !minimal_de_event(&press) ||
            !trait_shell_root_menu_open() ||
            !trait_shell_root_menu_bounds(&menu)) {
        fputs("WVRM Files root menu did not open\n", stderr);
        return 1;
    }
    press.point.x = (int32_t)(menu.x + 20U);
    press.point.y = (int32_t)(menu.y + TRAIT_MENU_TITLE_HEIGHT + 30U);
    if (!minimal_de_event(&press) || trait_shell_root_menu_open() ||
            trait_shell_window_count() != 1U ||
            trait_shell_app_of(trait_shell_focused()) != TRAIT_APP_FILES) {
        fputs("WVRM Files root menu did not launch Files\n", stderr);
        return 1;
    }
    {
        uint32_t data = trait_files_add(trait_files_root(), "Data", true, 0U);
        uint32_t file = trait_files_add(data, "note.txt", false, 5U);
        struct trait_rect cell;
        struct ui_event open = {
            .type = UI_EVENT_POINTER_BUTTON_PRESS,
            .button = UI_POINTER_BUTTON_LEFT,
            .double_click = true
        };

        trait_files_select(file, false);
        trait_files_set_read_only(true);
        if (data >= TRAIT_FILES_MAX_NODES || file >= TRAIT_FILES_MAX_NODES ||
                trait_shell_context_row_count() != 1U ||
                trait_files_copy_selection(false) ||
                trait_files_rename(file, "changed.txt") ||
                trait_files_remove(file) ||
                trait_files_move(file, trait_files_root()) ||
                trait_files_paste_into(data) != 0U ||
                trait_files_child_count(data) != 1U) {
            fputs("WVRM Files read-only boundary failed\n", stderr);
            return 1;
        }
        if (!trait_files_entry_bounds(
                trait_shell_window(trait_shell_focused()), 0U, &cell)) {
            fputs("WVRM Files folder has no hit target\n", stderr);
            return 1;
        }
        open.point.x = (int32_t)(cell.x + 30U);
        open.point.y = (int32_t)(cell.y + 5U);
        if (!minimal_de_event(&open) || trait_files_here() != data) {
            fputs("WVRM Files double click did not open folder\n", stderr);
            return 1;
        }
        {
            struct ui_event back = {
                .type = UI_EVENT_TEXT_INPUT,
                .character = '\b'
            };

            if (!minimal_de_event(&back) ||
                    trait_files_here() != trait_files_root()) {
                fputs("WVRM Files Backspace did not go up\n", stderr);
                return 1;
            }
        }
    }
    /* Revoking an account session clears names even if Files was open. */
    trait_files_reset();
    trait_files_set_read_only(true);
    if (trait_files_child_count(trait_files_root()) != 0U ||
            trait_files_here() != trait_files_root() ||
            trait_files_node_name(1U)[0] != '\0' ||
            !trait_files_read_only()) {
        fputs("WVRM Files retained an authenticated snapshot\n", stderr);
        return 1;
    }
    puts("minimal desktop host test passed");
    return 0;
}
