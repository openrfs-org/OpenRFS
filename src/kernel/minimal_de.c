/* SPDX-License-Identifier: GPL-3.0-only */
#include <openrfs/minimal_de.h>

#include <trait/files.h>
#include <trait/input.h>
#include <trait/menu.h>
#include <trait/packages.h>
#include <trait/settings.h>
#include <trait/shell.h>
#include <trait/taskmgr.h>
#include <trait/terminal.h>
#include <trait/window.h>

static struct trait_surface minimal_surface;

bool minimal_de_construct(uint32_t *pixels, uint32_t width, uint32_t height)
{
    if (pixels == NULL || width < 800U || height < 600U ||
            width > 4096U || height > 4096U) {
        return false;
    }
    minimal_surface = (struct trait_surface){ pixels, width, height };
    trait_files_reset();
    trait_packages_reset();
    trait_settings_reset();
    trait_taskmgr_reset();
    trait_terminal_reset();
    trait_menu_reset();
    if (!trait_menu_add("xterm", false, false) ||
            !trait_menu_add("glxgears", false, false) ||
            !trait_menu_add("", false, true) ||
            !trait_menu_add("Run...", false, false)) {
        return false;
    }
    trait_shell_reset(&minimal_surface);
    trait_shell_set_screen((struct trait_rect){ 0U, 0U, width, height });
    trait_shell_set_desktop_folder(trait_files_root());
    return trait_shell_open(TRAIT_APP_TERMINAL,
        (struct trait_rect){ 80U, 80U,
            width > 760U ? 560U : width - 120U,
            height > 480U ? 340U : height - 120U }) <
        TRAIT_SHELL_MAX_WINDOWS;
}

void minimal_de_draw(void)
{
    trait_shell_draw_root();
    trait_shell_draw_desktop();
    trait_shell_draw_window_icons();
    trait_shell_draw();
}

void minimal_de_draw_overlays(void)
{
    trait_shell_draw_overlays();
}

bool minimal_de_terminal_client(struct ui_rect *out)
{
    const uint32_t focused = trait_shell_focused();
    const struct trait_window *window;
    struct trait_rect client;

    if (out == NULL || focused >= TRAIT_SHELL_MAX_WINDOWS ||
            trait_shell_app_of(focused) != TRAIT_APP_TERMINAL) {
        return false;
    }
    window = trait_shell_window(focused);
    if (window == NULL || window->minimised) {
        return false;
    }
    client = trait_window_client(window);
    *out = (struct ui_rect){ client.x, client.y, client.width,
        client.height };
    return true;
}

bool minimal_de_overlay_open(void)
{
    return trait_shell_root_menu_open() || trait_shell_run_open() ||
        trait_shell_context_open() || trait_shell_rename_open();
}

bool minimal_de_event(const struct ui_event *event)
{
    struct trait_event translated = { 0 };

    if (event == NULL) {
        return false;
    }
    translated.x = event->point.x < 0 ? 0U : (uint32_t)event->point.x;
    translated.y = event->point.y < 0 ? 0U : (uint32_t)event->point.y;
    translated.modifiers = event->control ? TRAIT_MOD_CTRL : 0U;
    translated.key = event->character;
    translated.secondary = event->button == UI_POINTER_BUTTON_RIGHT;
    switch (event->type) {
    case UI_EVENT_POINTER_MOVEMENT:
        translated.kind = TRAIT_EVENT_POINTER_MOVE;
        break;
    case UI_EVENT_POINTER_BUTTON_PRESS:
        translated.kind = TRAIT_EVENT_POINTER_DOWN;
        break;
    case UI_EVENT_POINTER_BUTTON_RELEASE:
        translated.kind = TRAIT_EVENT_POINTER_UP;
        break;
    case UI_EVENT_TEXT_INPUT:
        translated.kind = TRAIT_EVENT_KEY;
        break;
    case UI_EVENT_KEYBOARD_FOCUS_NEXT:
        translated.kind = TRAIT_EVENT_KEY;
        translated.special = TRAIT_KEY_TAB;
        translated.modifiers |= TRAIT_MOD_ALT;
        break;
    case UI_EVENT_KEYBOARD_FOCUS_PREVIOUS:
        translated.kind = TRAIT_EVENT_KEY;
        translated.special = TRAIT_KEY_TAB;
        translated.modifiers |= TRAIT_MOD_ALT | TRAIT_MOD_SHIFT;
        break;
    case UI_EVENT_KEYBOARD_ACTIVATION:
        translated.kind = TRAIT_EVENT_KEY;
        translated.special = TRAIT_KEY_ENTER;
        break;
    case UI_EVENT_PANEL_CLOSE:
        translated.kind = TRAIT_EVENT_KEY;
        translated.special = TRAIT_KEY_F4;
        translated.modifiers |= TRAIT_MOD_ALT;
        break;
    case UI_EVENT_REDRAW_REQUEST:
        return true;
    default:
        return false;
    }
    if (translated.kind == TRAIT_EVENT_POINTER_DOWN &&
            !minimal_de_overlay_open() &&
            trait_shell_at(translated.x, translated.y) >=
                TRAIT_SHELL_MAX_WINDOWS) {
        return trait_shell_root_press(translated.x, translated.y);
    }
    return trait_shell_handle(&translated);
}

bool minimal_de_self_test(void)
{
    return trait_shell_self_test() && trait_terminal_self_test() &&
        trait_files_self_test() && trait_menu_self_test() &&
        trait_packages_self_test() && trait_settings_self_test() &&
        trait_taskmgr_self_test();
}
