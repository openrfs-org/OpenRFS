/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stdint.h>

#include <openrfs/console.h>
#include <openrfs/installer_ui.h>
#include <openrfs/screen.h>
#include <orfs/install.h>
#include <orfs/line.h>

/*
 * Kernel adapter for the installer UI imported from opengatcommandline.
 * The imported state machine collects and validates a proposed installation.
 * Its commit screen is deliberately preview-only until OpenRFS has a storage
 * transaction that can partition, format, copy, verify, and roll back.
 */
static struct orfs_install installer;
static bool active;

static void clear_secrets(void)
{
    volatile char *root_password = installer.rootpw;
    volatile char *user_password = installer.userpw;

    for (uint32_t index = 0U; index < ORFS_UI_VALUE; ++index) {
        root_password[index] = '\0';
        user_password[index] = '\0';
    }
    for (uint32_t field = 0U; field < ORFS_UI_FIELDS; ++field) {
        if (installer.ui.field[field].secret) {
            volatile char *value = installer.ui.field[field].value;

            for (uint32_t index = 0U; index < ORFS_UI_VALUE; ++index) {
                value[index] = '\0';
            }
        }
    }
}

static char display_glyph(char character)
{
    switch ((uint8_t)character) {
    case 0x80U: return '-';
    case 0x81U: return '|';
    case 0x82U: case 0x83U: case 0x84U: case 0x85U:
    case 0x86U: case 0x87U: case 0x88U: case 0x89U:
    case 0x8aU: return '+';
    case 0x8bU: return '.';
    case 0x8cU: return '#';
    case 0x8dU: return '^';
    case 0x8eU: return 'v';
    default: return character;
    }
}

static void render(void)
{
    char cells[ORFS_ROWS * ORFS_COLS];
    uint8_t attributes[ORFS_ROWS * ORFS_COLS];

    for (uint32_t row = 0U; row < ORFS_ROWS; ++row) {
        for (uint32_t column = 0U; column < ORFS_COLS; ++column) {
            const uint32_t at = row * ORFS_COLS + column;
            cells[at] = display_glyph(orfs_term_at(&installer.term, row, column));
            attributes[at] = orfs_term_attr_at(&installer.term, row, column);
        }
    }
    (void)screen_draw_text_grid(cells, attributes, ORFS_COLS, ORFS_ROWS);
}

static int translated_key(const struct keyboard_event *event)
{
    if (event->character != '\0') {
        return (int)(unsigned char)event->character;
    }
    switch (event->scancode) {
    case 0x48U: return ORFS_KEY_UP;
    case 0x50U: return ORFS_KEY_DOWN;
    case 0x4bU: return ORFS_KEY_LEFT;
    case 0x4dU: return ORFS_KEY_RIGHT;
    case 0x47U: return ORFS_KEY_HOME;
    case 0x4fU: return ORFS_KEY_END;
    case 0x53U: return ORFS_KEY_DEL;
    case 0x49U: return ORFS_KEY_PGUP;
    case 0x51U: return ORFS_KEY_PGDN;
    case 0x3bU: return ORFS_KEY_F1;
    case 0x01U: return 0x1b;
    default: return 0;
    }
}

void installer_ui_begin(void)
{
    orfs_install_begin(&installer);
    active = true;
    render();
}

bool installer_ui_is_active(void)
{
    return active;
}

void installer_ui_handle_keyboard(const struct keyboard_event *event)
{
    int key;

    if (!active || event == NULL || !event->pressed) {
        return;
    }
    key = translated_key(event);
    if (key != 0) {
        orfs_install_key(&installer, key);
        render();
    }
}

void installer_ui_pump(void)
{
    if (!active) {
        return;
    }
    if (installer.step == ORFS_STEP_WRITE || installer.step == ORFS_STEP_VERIFY) {
        orfs_install_tick(&installer);
        render();
    }
    if (installer.step == ORFS_STEP_SHELL ||
            installer.step == ORFS_STEP_ABANDONED ||
            installer.step == ORFS_STEP_REBOOT) {
        clear_secrets();
        active = false;
        (void)screen_clear();
        console_write("Installer preview closed. No disk was changed.\nopenrfs$ ");
    }
}
