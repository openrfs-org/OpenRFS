/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/rsd_desktop.h>
#include <rsd/fat32_fs.h>
#include <rsd/heap.h>
#include <rsd/package_service.h>

#include <rsd_desktop/files.h>
#include <rsd_desktop/input.h>
#include <rsd_desktop/menu.h>
#include <rsd_desktop/packages.h>
#include <rsd_desktop/settings.h>
#include <rsd_desktop/shell.h>
#include <rsd_desktop/taskmgr.h>
#include <rsd_desktop/terminal.h>
#include <rsd_desktop/window.h>

static struct rsd_surface minimal_surface;
static struct rsdfs_list_entry live_file_entries[RSD_FILES_MAX_CHILDREN];

static void package_text(char *out, size_t capacity,
    struct package_state_text value)
{
    size_t at = 0U;

    while (at < value.length && at + 1U < capacity) {
        out[at] = (char)value.bytes[at];
        ++at;
    }
    out[at] = '\0';
}

static bool load_installed_packages(void)
{
    uint8_t *bytes = NULL;
    size_t length = 0U;
    struct package_service_report report;
    struct package_state_database_view database;
    bool loaded = false;

    if (heap_allocate(PACKAGE_SERVICE_MAX_DATABASE_BYTES,
            (void **)&bytes) != HEAP_STATUS_OK) {
        return false;
    }
    if (package_service_snapshot(bytes, PACKAGE_SERVICE_MAX_DATABASE_BYTES,
            &length, &report) != PACKAGE_SERVICE_STATUS_OK ||
            package_state_database_parse(bytes, length, &database) !=
                PACKAGE_STATE_STATUS_OK ||
            database.package_count > RSD_PACKAGES_MAX) {
        goto release;
    }
    for (uint32_t at = 0U; at < database.package_count; ++at) {
        struct package_state_package_view package;
        char name[RSD_PACKAGES_NAME_BYTES];
        char version[RSD_PACKAGES_TEXT_BYTES];

        if (package_state_database_package(&database, at, &package) !=
                PACKAGE_STATE_STATUS_OK) {
            goto release;
        }
        package_text(name, sizeof(name), package.identifier);
        package_text(version, sizeof(version), package.version);
        if (!rsd_packages_add(name, version, "", true)) {
            goto release;
        }
    }
    loaded = true;

release:
    (void)heap_free(bytes);
    return loaded;
}

static bool list_data_folder(const char *path,
    struct rsd_files_source_entry *entries, uint32_t capacity,
    uint32_t *count)
{
    size_t found = 0U;

    if (path == NULL || entries == NULL || count == NULL ||
            capacity > RSD_FILES_MAX_CHILDREN ||
            rsdfs_list(RSDFS_VOLUME_DATA, path, live_file_entries,
                capacity, &found) != RSDFS_STATUS_OK || found > capacity) {
        return false;
    }
    for (size_t at = 0U; at < found; ++at) {
        size_t length = 0U;

        while (length < sizeof(live_file_entries[at].name) &&
                live_file_entries[at].name[length] != '\0') {
            ++length;
        }
        if (length == 0U || length >= RSD_FILES_NAME_BYTES ||
                length == sizeof(live_file_entries[at].name)) {
            return false;
        }
        for (size_t index = 0U; index <= length; ++index) {
            entries[at].name[index] = live_file_entries[at].name[index];
        }
        entries[at].folder = live_file_entries[at].directory;
        entries[at].bytes = live_file_entries[at].size > UINT32_MAX ?
            UINT32_MAX : (uint32_t)live_file_entries[at].size;
    }
    *count = (uint32_t)found;
    return true;
}

static bool rename_data_path(const char *from, const char *to)
{
    return rsdfs_rename(RSDFS_VOLUME_DATA, from, to) == RSDFS_STATUS_OK;
}

static bool remove_data_path(const char *path, bool folder)
{
    return (folder ? rsdfs_rmdir(RSDFS_VOLUME_DATA, path) :
        rsdfs_unlink(RSDFS_VOLUME_DATA, path)) == RSDFS_STATUS_OK;
}

static bool data_child_path(const char *parent, const char *name,
    char out[RSDFS_MAX_PATH])
{
    size_t at = 0U;
    size_t next = 0U;

    if (name[0] == '.' && (name[1] == '\0' ||
            (name[1] == '.' && name[2] == '\0'))) {
        return false;
    }
    while (parent[at] != '\0' && at + 1U < RSDFS_MAX_PATH) {
        out[at] = parent[at];
        ++at;
    }
    if (parent[at] != '\0' || at == 0U) {
        return false;
    }
    if (out[at - 1U] != '/') {
        if (at + 1U >= RSDFS_MAX_PATH) {
            return false;
        }
        out[at++] = '/';
    }
    while (name[next] != '\0' && at + 1U < RSDFS_MAX_PATH) {
        if (name[next] == '/' || name[next] == '\\') {
            return false;
        }
        out[at++] = name[next++];
    }
    if (name[next] != '\0' || next == 0U) {
        return false;
    }
    out[at] = '\0';
    return true;
}

static bool remove_data_tree(const char *path, uint32_t depth)
{
    struct rsdfs_stat stat;

    if (depth > RSDFS_MAX_DEPTH ||
            rsdfs_lstat_path(RSDFS_VOLUME_DATA, path, &stat) !=
                RSDFS_STATUS_OK) {
        return false;
    }
    if (!stat.directory) {
        return rsdfs_unlink(RSDFS_VOLUME_DATA, path) == RSDFS_STATUS_OK;
    }
    rsdfs_directory_handle directory;
    if (rsdfs_directory_open(RSDFS_VOLUME_DATA, path, &directory) !=
            RSDFS_STATUS_OK) {
        return false;
    }
    bool good = true;
    for (;;) {
        struct rsdfs_list_entry entry;
        bool present = false;

        if (rsdfs_directory_read(directory, &entry, &present) !=
                RSDFS_STATUS_OK) {
            good = false;
            break;
        }
        if (!present) {
            break;
        }
        char child[RSDFS_MAX_PATH];
        if (!data_child_path(path, entry.name, child) ||
                !remove_data_tree(child, depth + 1U)) {
            good = false;
            break;
        }
    }
    if (rsdfs_directory_close(directory) != RSDFS_STATUS_OK) {
        good = false;
    }
    return good && rsdfs_rmdir(RSDFS_VOLUME_DATA, path) == RSDFS_STATUS_OK;
}

static bool copy_data_tree(const char *from, const char *to, uint32_t depth)
{
    struct rsdfs_stat stat;

    if (depth > RSDFS_MAX_DEPTH ||
            rsdfs_lstat_path(RSDFS_VOLUME_DATA, from, &stat) !=
                RSDFS_STATUS_OK) {
        return false;
    }
    if (stat.directory) {
        rsdfs_directory_handle directory;
        bool good = true;

        if (rsdfs_mkdir_mode(RSDFS_VOLUME_DATA, to,
                (uint16_t)(stat.mode & UINT16_C(0777))) != RSDFS_STATUS_OK) {
            return false;
        }
        if (rsdfs_directory_open(RSDFS_VOLUME_DATA, from, &directory) !=
                RSDFS_STATUS_OK) {
            good = false;
        } else {
            for (;;) {
                struct rsdfs_list_entry entry;
                bool present = false;
                char source[RSDFS_MAX_PATH];
                char destination[RSDFS_MAX_PATH];

                if (rsdfs_directory_read(directory, &entry, &present) !=
                        RSDFS_STATUS_OK) {
                    good = false;
                    break;
                }
                if (!present) {
                    break;
                }
                if (!data_child_path(from, entry.name, source) ||
                        !data_child_path(to, entry.name, destination) ||
                        !copy_data_tree(source, destination, depth + 1U)) {
                    good = false;
                    break;
                }
            }
            if (rsdfs_directory_close(directory) != RSDFS_STATUS_OK) {
                good = false;
            }
        }
        if (!good) {
            (void)remove_data_tree(to, depth);
        }
        return good;
    }

    rsdfs_handle input;
    rsdfs_handle output;
    if (rsdfs_open(RSDFS_VOLUME_DATA, from, RSDFS_ACCESS_READ, &input) !=
            RSDFS_STATUS_OK) {
        return false;
    }
    if (rsdfs_open_options(RSDFS_VOLUME_DATA, to, RSDFS_ACCESS_WRITE,
            RSDFS_OPEN_CREATE | RSDFS_OPEN_EXCLUSIVE,
            (uint16_t)(stat.mode & UINT16_C(0777)), &output) !=
            RSDFS_STATUS_OK) {
        (void)rsdfs_close(input);
        return false;
    }
    bool good = true;
    for (;;) {
        uint8_t buffer[4096U];
        size_t read_bytes = 0U;
        size_t written = 0U;

        if (rsdfs_read(input, buffer, sizeof(buffer), &read_bytes) !=
                RSDFS_STATUS_OK) {
            good = false;
            break;
        }
        if (read_bytes == 0U) {
            break;
        }
        while (written < read_bytes) {
            size_t piece = 0U;

            if (rsdfs_write(output, buffer + written,
                    read_bytes - written, &piece) != RSDFS_STATUS_OK ||
                    piece == 0U) {
                good = false;
                break;
            }
            written += piece;
        }
        if (!good) {
            break;
        }
    }
    if (good && rsdfs_fsync(output) != RSDFS_STATUS_OK) {
        good = false;
    }
    if (rsdfs_close(output) != RSDFS_STATUS_OK) {
        good = false;
    }
    if (rsdfs_close(input) != RSDFS_STATUS_OK) {
        good = false;
    }
    if (!good) {
        (void)rsdfs_unlink(RSDFS_VOLUME_DATA, to);
    }
    return good;
}

static bool copy_data_path(const char *from, const char *to, bool folder)
{
    struct rsdfs_stat stat;

    if (rsdfs_lstat_path(RSDFS_VOLUME_DATA, from, &stat) !=
            RSDFS_STATUS_OK || stat.directory != folder ||
            !copy_data_tree(from, to, 0U)) {
        return false;
    }
    if (rsdfs_sync(RSDFS_VOLUME_DATA) != RSDFS_STATUS_OK) {
        (void)remove_data_tree(to, 0U);
        return false;
    }
    return true;
}

bool rsd_desktop_construct(uint32_t *pixels, uint32_t width, uint32_t height)
{
    if (pixels == NULL || width < 800U || height < 600U ||
            width > 4096U || height > 4096U) {
        return false;
    }
    minimal_surface = (struct rsd_surface){ pixels, width, height };
    rsd_files_reset();
    rsd_files_use_live_source(list_data_folder);
    rsd_files_use_live_writes(rename_data_path, remove_data_path);
    rsd_files_use_live_copy(copy_data_path);
    (void)rsd_files_open(rsd_files_root());
    rsd_packages_reset();
    rsd_packages_use_live_source(load_installed_packages);
    (void)rsd_packages_refresh();
    rsd_settings_reset();
    rsd_taskmgr_reset();
    rsd_terminal_reset();
    rsd_menu_reset();
    if (!rsd_menu_add("xterm", false, false) ||
            !rsd_menu_add("Files", false, false) ||
            !rsd_menu_add("Packages", false, false) ||
            !rsd_menu_add("Task Manager", false, false) ||
            !rsd_menu_add("Settings", false, false) ||
            !rsd_menu_add("", false, true) ||
            !rsd_menu_add("Run...", false, false)) {
        return false;
    }
    rsd_shell_reset(&minimal_surface);
    rsd_shell_set_screen((struct rsd_rect){ 0U, 0U, width, height });
    rsd_shell_set_desktop_folder(rsd_files_root());
    return rsd_shell_open(RSD_APP_TERMINAL,
        (struct rsd_rect){ 80U, 80U,
            width > 760U ? 560U : width - 120U,
            height > 480U ? 340U : height - 120U }) <
        RSD_SHELL_MAX_WINDOWS;
}

void rsd_desktop_draw(void)
{
    rsd_shell_draw_root();
    rsd_shell_draw_desktop();
    rsd_shell_draw_window_icons();
    rsd_shell_draw();
}

void rsd_desktop_draw_overlays(void)
{
    rsd_shell_draw_overlays();
}

bool rsd_desktop_terminal_client(struct ui_rect *out)
{
    const uint32_t focused = rsd_shell_focused();
    const struct rsd_window *window;
    struct rsd_rect client;

    if (out == NULL || focused >= RSD_SHELL_MAX_WINDOWS ||
            rsd_shell_app_of(focused) != RSD_APP_TERMINAL) {
        return false;
    }
    window = rsd_shell_window(focused);
    if (window == NULL || window->minimised) {
        return false;
    }
    client = rsd_window_client(window);
    *out = (struct ui_rect){ client.x, client.y, client.width,
        client.height };
    return true;
}

bool rsd_desktop_overlay_open(void)
{
    return rsd_shell_root_menu_open() || rsd_shell_run_open() ||
        rsd_shell_context_open() || rsd_shell_rename_open();
}

bool rsd_desktop_event(const struct ui_event *event)
{
    struct rsd_event translated = { 0 };

    if (event == NULL) {
        return false;
    }
    translated.x = event->point.x < 0 ? 0U : (uint32_t)event->point.x;
    translated.y = event->point.y < 0 ? 0U : (uint32_t)event->point.y;
    translated.modifiers = event->control ? RSD_MOD_CTRL : 0U;
    translated.key = event->character;
    translated.secondary = event->button == UI_POINTER_BUTTON_RIGHT;
    switch (event->type) {
    case UI_EVENT_POINTER_MOVEMENT:
        translated.kind = RSD_EVENT_POINTER_MOVE;
        break;
    case UI_EVENT_POINTER_BUTTON_PRESS:
        translated.kind = RSD_EVENT_POINTER_DOWN;
        break;
    case UI_EVENT_POINTER_BUTTON_RELEASE:
        translated.kind = RSD_EVENT_POINTER_UP;
        break;
    case UI_EVENT_TEXT_INPUT:
        translated.kind = RSD_EVENT_KEY;
        break;
    case UI_EVENT_KEYBOARD_FOCUS_NEXT:
        translated.kind = RSD_EVENT_KEY;
        translated.special = RSD_KEY_TAB;
        translated.modifiers |= RSD_MOD_ALT;
        break;
    case UI_EVENT_KEYBOARD_FOCUS_PREVIOUS:
        translated.kind = RSD_EVENT_KEY;
        translated.special = RSD_KEY_TAB;
        translated.modifiers |= RSD_MOD_ALT | RSD_MOD_SHIFT;
        break;
    case UI_EVENT_KEYBOARD_ACTIVATION:
        translated.kind = RSD_EVENT_KEY;
        translated.special = RSD_KEY_ENTER;
        break;
    case UI_EVENT_PANEL_CLOSE:
        translated.kind = RSD_EVENT_KEY;
        translated.special = RSD_KEY_F4;
        translated.modifiers |= RSD_MOD_ALT;
        break;
    case UI_EVENT_REDRAW_REQUEST:
        return true;
    default:
        return false;
    }
    if (translated.kind == RSD_EVENT_POINTER_DOWN &&
            !rsd_desktop_overlay_open() &&
            rsd_shell_at(translated.x, translated.y) >=
                RSD_SHELL_MAX_WINDOWS) {
        return rsd_shell_root_press(translated.x, translated.y);
    }
    return rsd_shell_handle(&translated);
}

bool rsd_desktop_self_test(void)
{
    return rsd_shell_self_test() && rsd_terminal_self_test() &&
        rsd_files_self_test() && rsd_menu_self_test() &&
        rsd_packages_self_test() && rsd_settings_self_test() &&
        rsd_taskmgr_self_test();
}
