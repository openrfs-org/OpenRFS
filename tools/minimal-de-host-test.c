/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>

#include <openrfs/minimal_de.h>
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
        .button = UI_POINTER_BUTTON_LEFT
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
            trait_shell_run_match_count() != 2U ||
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
    puts("minimal desktop host test passed");
    return 0;
}
