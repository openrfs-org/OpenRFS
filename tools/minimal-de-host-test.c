/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>

#include <rsd/minimal_de.h>
#include <rsd_desktop/menu.h>
#include <rsd_desktop/shell.h>

static uint32_t pixels[1024U * 768U];

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

    if (minimal_de_construct(pixels, 799U, 768U) ||
            !minimal_de_self_test() ||
            !minimal_de_construct(pixels, 1024U, 768U) ||
            !minimal_de_terminal_client(&terminal) ||
            rsd_menu_row_count() != 7U ||
            rsd_shell_run_match_count() != 5U ||
            terminal.x != 84U || terminal.y != 108U ||
            terminal.width != 552U || terminal.height != 308U) {
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
    if (rsd_shell_open(RSD_APP_TERMINAL,
            (struct rsd_rect){ 400U, 400U, 300U, 200U }) >=
                RSD_SHELL_MAX_WINDOWS ||
            rsd_shell_window_count() != 2U ||
            !minimal_de_terminal_client(&terminal) || terminal.x < 400U) {
        fputs("second RSD terminal did not open\n",
            stderr);
        return 1;
    }
    (void)minimal_de_event(&move);
    if (!minimal_de_terminal_client(&terminal) || terminal.x < 400U) {
        fputs("pointer hover moved the terminal without a drag\n", stderr);
        return 1;
    }
    if (!minimal_de_event(&close) ||
            rsd_shell_window_count() != 1U ||
            !minimal_de_terminal_client(&terminal) ||
            !minimal_de_event(&close) ||
            rsd_shell_window_count() != 0U ||
            !minimal_de_event(&press) ||
            !rsd_shell_root_menu_open() ||
            !rsd_shell_root_menu_bounds(&menu)) {
        fputs("minimal desktop close or root menu failed\n", stderr);
        return 1;
    }
    press.point.x = (int32_t)(menu.x + 20U);
    press.point.y = (int32_t)(menu.y + RSD_MENU_TITLE_HEIGHT + 10U);
    if (!minimal_de_event(&press) || rsd_shell_root_menu_open() ||
            !minimal_de_terminal_client(&terminal)) {
        fputs("minimal desktop terminal relaunch failed\n", stderr);
        return 1;
    }
    puts("minimal desktop host test passed");
    return 0;
}
