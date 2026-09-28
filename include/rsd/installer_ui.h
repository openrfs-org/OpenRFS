/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_INSTALLER_UI_H
#define RSD_INSTALLER_UI_H

#include <stdbool.h>
#include <rsd/keyboard.h>

/* Keyboard-driven RSD installer preview. */
void installer_ui_begin(void);
bool installer_ui_is_active(void);
void installer_ui_handle_keyboard(const struct keyboard_event *event);
void installer_ui_pump(void);

#endif
