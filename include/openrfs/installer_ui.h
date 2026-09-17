/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_INSTALLER_UI_H
#define OPENRFS_INSTALLER_UI_H

#include <stdbool.h>
#include <openrfs/keyboard.h>

/* Interactive installer preview ported from opengatcommandline. */
void installer_ui_begin(void);
bool installer_ui_is_active(void);
void installer_ui_handle_keyboard(const struct keyboard_event *event);
void installer_ui_pump(void);

#endif
