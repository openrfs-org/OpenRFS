/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/boot_ledger.h>
#include <openrfs/account.h>
#include <openrfs/clock.h>
#include <openrfs/de/files.h>
#include <openrfs/de/menu.h>
#include <openrfs/de/packages.h>
#include <openrfs/de/panel.h>
#include <openrfs/de/settings.h>
#include <openrfs/de/shell.h>
#include <openrfs/de/surface.h>
#include <openrfs/de/taskmgr.h>
#include <openrfs/de/terminal.h>
#include <openrfs/de/theme.h>
#include <openrfs/de/window.h>
#include <openrfs/framebuffer.h>
#include <openrfs/fat32_fs.h>
#include <openrfs/heap.h>
#include <openrfs/hwdrv.h>
#include <openrfs/minimal_de.h>
#include <openrfs/pointer.h>
#include <openrfs/screen.h>
#include <openrfs/surface.h>
#include <openrfs/ui.h>
#include <openrfs/ui_font.h>
#include <openrfs/wallpaper.h>
#include <trait/files.h>
#include <trait/shell.h>

#define UI_MIN_WIDTH 800U
#define UI_MIN_HEIGHT 600U
#define UI_MAX_WIDTH 4096U
#define UI_FNV_OFFSET UINT64_C(14695981039346656037)
#define UI_FNV_PRIME UINT64_C(1099511628211)

struct native_window_record {
    bool open;
    bool capture;
    const uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    struct openrfs_window window;
    ui_native_event_fn handler;
    void *context;
};

static struct ui_state state;
static struct surface *canvas;
static struct openrfs_surface desktop;
static uint32_t *desktop_pixels;
static uint32_t converted_row[UI_MAX_WIDTH];
static struct ui_event queue[UI_EVENT_QUEUE_CAPACITY];
static size_t queue_read;
static size_t queue_write;
static bool redraw_pending;
static const char *self_test_failure = "OpenRFS desktop self-test has not run";
static const char *installed_failure = "OpenRFS desktop proof has not run";
static struct native_window_record native_windows[UI_NATIVE_WINDOW_COUNT];
static int32_t native_focus = -1;
static bool minimal_desktop_selected;
static bool wvrm_last_click_valid;
static uint64_t wvrm_last_click_ns;
static struct ui_point wvrm_last_click_point;
static struct openrfsfs_list_entry wvrm_directory_entries[OPENRFSFS_MAX_LIST_ENTRIES];

/* WVRM Files is an authenticated, read-only snapshot of mounted volumes. */
static bool wvrm_files_load(uint32_t folder)
{
    char path[TRAIT_FILES_PATH_BYTES];
    const char *relative;
    enum openrfsfs_volume volume;
    size_t count = 0U;
    size_t at;

    if (!account_session_active()) {
        trait_shell_notify("WVRM Files", "Sign in to browse files");
        return false;
    }
    trait_files_path(folder, path, sizeof(path));
    if (path[0] != '/') {
        return false;
    }
    if (path[1] == 'S' && path[2] == 'y' && path[3] == 's' &&
            path[4] == 't' && path[5] == 'e' && path[6] == 'm' &&
            (path[7] == '/' || path[7] == '\0')) {
        volume = OPENRFSFS_VOLUME_SYSTEM;
        relative = path + 7U;
    } else if (path[1] == 'D' && path[2] == 'a' && path[3] == 't' &&
            path[4] == 'a' && (path[5] == '/' || path[5] == '\0')) {
        volume = OPENRFSFS_VOLUME_DATA;
        relative = path + 5U;
    } else {
        return false;
    }
    if (*relative == '/') {
        ++relative;
    }
    if (*relative == '\0') {
        relative = ".";
    }
    if (openrfsfs_list(volume, relative, wvrm_directory_entries,
            OPENRFSFS_MAX_LIST_ENTRIES, &count) != OPENRFSFS_STATUS_OK) {
        trait_shell_notify("WVRM Files", "Directory unavailable");
        return false;
    }
    if (count > TRAIT_FILES_MAX_CHILDREN ||
            count > trait_files_free_slots()) {
        trait_shell_notify("WVRM Files", "Directory exceeds view limit");
        return false;
    }
    for (at = 0U; at < count; ++at) {
        const char *name = wvrm_directory_entries[at].name;
        size_t length = 0U;

        while (length < OPENRFSFS_MAX_COMPONENT_BYTES &&
                name[length] != '\0') {
            if (name[length] == '/') {
                return false;
            }
            ++length;
        }
        if (length == 0U || length >= TRAIT_FILES_NAME_BYTES ||
                (name[0] == '.' && (name[1] == '\0' ||
                (name[1] == '.' && name[2] == '\0')))) {
            trait_shell_notify("WVRM Files", "Invalid directory entry");
            return false;
        }
    }
    for (at = 0U; at < count; ++at) {
        const struct openrfsfs_list_entry *entry = &wvrm_directory_entries[at];
        const uint32_t bytes = entry->size > UINT32_MAX ?
            UINT32_MAX : (uint32_t)entry->size;

        if (trait_files_add(folder, entry->name, entry->directory, bytes) >=
                TRAIT_FILES_MAX_NODES) {
            trait_shell_notify("WVRM Files", "Directory exceeds view limit");
            return false;
        }
    }
    return true;
}

static void zero_bytes(void *pointer, size_t bytes)
{
    uint8_t *out = pointer;

    for (size_t at = 0U; at < bytes; ++at) {
        out[at] = 0U;
    }
}

static void copy_text(char *out, size_t capacity, const char *text)
{
    size_t at = 0U;

    if (capacity == 0U) {
        return;
    }
    while (text != NULL && text[at] != '\0' && at + 1U < capacity) {
        out[at] = text[at];
        ++at;
    }
    out[at] = '\0';
}

static bool rect_fits(struct ui_rect rectangle, struct ui_rect outer)
{
    return rectangle.width != 0U && rectangle.height != 0U &&
        rectangle.x >= outer.x && rectangle.y >= outer.y &&
        rectangle.width <= outer.width && rectangle.height <= outer.height &&
        rectangle.x - outer.x <= outer.width - rectangle.width &&
        rectangle.y - outer.y <= outer.height - rectangle.height;
}

static bool rect_contains(struct ui_rect box, int32_t x, int32_t y)
{
    return x >= 0 && y >= 0 && (uint32_t)x >= box.x &&
        (uint32_t)y >= box.y && (uint32_t)x - box.x < box.width &&
        (uint32_t)y - box.y < box.height;
}

static struct ui_rect ui_rect_from_openrfs(struct openrfs_rect rectangle)
{
    return (struct ui_rect){ rectangle.x, rectangle.y,
        rectangle.width, rectangle.height };
}

static enum ui_element_id focus_for_index(size_t index)
{
    return (enum ui_element_id)(UI_ELEMENT_DOCK_FILES + index);
}

static enum ui_panel_id panel_for_app(enum openrfs_shell_app app)
{
    switch (app) {
    case OPENRFS_APP_FILES:
        return UI_PANEL_FILES;
    case OPENRFS_APP_TERMINAL:
        return UI_PANEL_TERMINAL;
    case OPENRFS_APP_TASKMGR:
        return UI_PANEL_TASKMGR;
    case OPENRFS_APP_SETTINGS:
        return UI_PANEL_SETTINGS;
    case OPENRFS_APP_PACKAGES:
        return UI_PANEL_PACKAGES;
    default:
        return UI_PANEL_NONE;
    }
}

static enum openrfs_shell_app app_for_focus(enum ui_element_id focus)
{
    switch (focus) {
    case UI_ELEMENT_DOCK_FILES:
        return OPENRFS_APP_FILES;
    case UI_ELEMENT_DOCK_TERMINAL:
        return OPENRFS_APP_TERMINAL;
    case UI_ELEMENT_DOCK_TASKMGR:
        return OPENRFS_APP_TASKMGR;
    case UI_ELEMENT_DOCK_PACKAGES:
        return OPENRFS_APP_PACKAGES;
    case UI_ELEMENT_DOCK_SETTINGS:
        return OPENRFS_APP_SETTINGS;
    default:
        return OPENRFS_APP_COUNT;
    }
}

static struct openrfs_rect default_window(enum openrfs_shell_app app)
{
    uint32_t width = desktop.width > 700U ? 640U : desktop.width - 80U;
    uint32_t height = desktop.height > 560U ? 480U : desktop.height - 80U;
    uint32_t offset = (uint32_t)app * 22U;

    if (app == OPENRFS_APP_SETTINGS || app == OPENRFS_APP_PACKAGES) {
        width = desktop.width > 760U ? 680U : desktop.width - 60U;
        height = desktop.height > 500U ? 420U : desktop.height - 70U;
    }
    return (struct openrfs_rect){ 38U + offset, 34U + offset, width, height };
}

static void set_theme(void)
{
    state.theme.white = framebuffer_pack(0xF8U, 0xFAU, 0xF8U);
    state.theme.ink = framebuffer_pack(0x18U, 0x21U, 0x24U);
    state.theme.desktop_dark = framebuffer_pack(0x25U, 0x2AU, 0x2FU);
    state.theme.desktop_light = framebuffer_pack(0x70U, 0x75U, 0x7AU);
    state.theme.title_active = framebuffer_pack(0x0EU, 0x49U, 0x76U);
    state.theme.title_inactive = framebuffer_pack(0x86U, 0x91U, 0x89U);
    state.theme.accent_teal = framebuffer_pack(0x6FU, 0x87U, 0x9EU);
    state.theme.accent_gold = framebuffer_pack(0x5CU, 0x7EU, 0x9CU);
    state.theme.accent_green = framebuffer_pack(0x4FU, 0x88U, 0x72U);
    state.theme.accent_red = framebuffer_pack(0xC9U, 0x55U, 0x4FU);
    state.theme.accent_violet = framebuffer_pack(0x8CU, 0x75U, 0xA8U);
    state.theme.shadow = framebuffer_pack(0x12U, 0x18U, 0x1DU);
    state.theme.window_face = framebuffer_pack(0xE1U, 0xE5U, 0xE8U);
}

enum ui_status ui_layout_build(uint32_t width, uint32_t height,
    struct ui_layout *layout)
{
    static const char *const labels[UI_DOCK_ITEM_COUNT] = {
        "Files", "Terminal", "Task Manager", "Packages", "Settings"
    };
    static const enum ui_action actions[UI_DOCK_ITEM_COUNT] = {
        UI_ACTION_OPEN_FILES, UI_ACTION_OPEN_TERMINAL, UI_ACTION_OPEN_TASKMGR,
        UI_ACTION_OPEN_PACKAGES, UI_ACTION_OPEN_SETTINGS
    };
    static const enum ui_panel_id panels[UI_DOCK_ITEM_COUNT] = {
        UI_PANEL_FILES, UI_PANEL_TERMINAL, UI_PANEL_TASKMGR,
        UI_PANEL_PACKAGES, UI_PANEL_SETTINGS
    };

    if (layout == NULL) {
        return UI_STATUS_NULL_ARGUMENT;
    }
    if (width < UI_MIN_WIDTH || height < UI_MIN_HEIGHT ||
            width > UI_MAX_WIDTH) {
        return UI_STATUS_UNSUPPORTED_GEOMETRY;
    }
    zero_bytes(layout, sizeof(*layout));
    layout->surface = (struct ui_rect){ 0U, 0U, width, height };
    layout->menu_bar = (struct ui_rect){ 0U, height - OPENRFS_PANEL_HEIGHT,
        width, OPENRFS_PANEL_HEIGHT };
    layout->workspace_bar = layout->menu_bar;
    layout->dock = layout->menu_bar;
    layout->panel = (struct ui_rect){ 38U, 34U, width - 76U,
        height - 94U };
    layout->panel_client = (struct ui_rect){ layout->panel.x + 1U,
        layout->panel.y + OPENRFS_TITLE_HEIGHT, layout->panel.width - 2U,
        layout->panel.height - OPENRFS_TITLE_HEIGHT - 1U };
    for (size_t at = 0U; at < UI_DOCK_ITEM_COUNT; ++at) {
        struct ui_dock_item *item = &layout->dock_items[at];

        item->id = focus_for_index(at);
        item->label = labels[at];
        item->action = actions[at];
        item->panel = panels[at];
        item->bounds = (struct ui_rect){ 30U + (uint32_t)at * 24U,
            height - OPENRFS_PANEL_HEIGHT, 24U, OPENRFS_PANEL_HEIGHT };
        item->icon_bounds = item->bounds;
    }
    return ui_layout_validate(layout);
}

enum ui_status ui_layout_validate(const struct ui_layout *layout)
{
    if (layout == NULL) {
        return UI_STATUS_NULL_ARGUMENT;
    }
    if (layout->surface.width < UI_MIN_WIDTH ||
            layout->surface.height < UI_MIN_HEIGHT ||
            layout->surface.width > UI_MAX_WIDTH) {
        return UI_STATUS_UNSUPPORTED_GEOMETRY;
    }
    if (!rect_fits(layout->menu_bar, layout->surface) ||
            !rect_fits(layout->panel, layout->surface) ||
            !rect_fits(layout->panel_client, layout->panel)) {
        return UI_STATUS_RECTANGLE_OUT_OF_BOUNDS;
    }
    for (size_t at = 0U; at < UI_DOCK_ITEM_COUNT; ++at) {
        const struct ui_dock_item *item = &layout->dock_items[at];

        if (item->id != focus_for_index(at) ||
                item->panel <= UI_PANEL_NONE || item->panel >= UI_PANEL_COUNT ||
                !rect_fits(item->bounds, layout->surface)) {
            return UI_STATUS_BAD_ELEMENT;
        }
    }
    return UI_STATUS_OK;
}

enum ui_status ui_hit_test(const struct ui_layout *layout,
    struct ui_point point, enum ui_element_id *element)
{
    if (layout == NULL || element == NULL) {
        return UI_STATUS_NULL_ARGUMENT;
    }
    *element = UI_ELEMENT_NONE;
    for (size_t at = 0U; at < UI_DOCK_ITEM_COUNT; ++at) {
        if (rect_contains(layout->dock_items[at].bounds, point.x, point.y)) {
            *element = layout->dock_items[at].id;
            break;
        }
    }
    return UI_STATUS_OK;
}

static uint32_t populate_files(void)
{
    uint32_t home;
    uint32_t user;
    uint32_t desktop_folder;
    uint32_t docs;

    openrfs_files_reset();
    home = openrfs_files_add(openrfs_files_root(), "home", true, 0U);
    user = openrfs_files_add(home, "user", true, 0U);
    desktop_folder = openrfs_files_add(user, "Desktop", true, 0U);
    docs = openrfs_files_add(user, "Documents", true, 0U);
    (void)openrfs_files_add(user, "Downloads", true, 0U);
    (void)openrfs_files_add(user, "README.txt", false, 1284U);
    (void)openrfs_files_add(docs, "privacy-notes.txt", false, 4096U);
    (void)openrfs_files_add(desktop_folder, "About OpenRFS.txt", false, 1024U);
    (void)openrfs_files_open(user);
    return desktop_folder;
}

static void populate_menu(void)
{
    openrfs_menu_reset();
    (void)openrfs_menu_add("OpenRFS", true, false);
    (void)openrfs_menu_add("System Tools", true, false);
    (void)openrfs_menu_add(NULL, false, true);
    (void)openrfs_menu_add("Run...", false, false);
}

static void populate_packages(void)
{
    openrfs_packages_reset();
    (void)openrfs_packages_add("openrfs-files", "OpenRFS file manager",
        "Files", true);
    (void)openrfs_packages_add("openrfs-terminal", "OpenRFS terminal",
        "Terminal", true);
    (void)openrfs_packages_add("openrfs-task-manager", "OpenRFS process viewer",
        "Task Manager", true);
    (void)openrfs_packages_add("openrfs-settings", "OpenRFS desktop settings",
        "Settings", true);
    (void)openrfs_packages_add("openrfs-privacy-tools", "Privacy tools bundle",
        "Privacy Tools", false);
}

static void populate_settings(void)
{
    struct openrfs_settings_row row;

    openrfs_settings_reset();
    (void)openrfs_settings_add_page("OpenRFS DE");
    (void)openrfs_settings_add_page("Desktop");
    (void)openrfs_settings_add_page("Panel");
    zero_bytes(&row, sizeof(row));
    row.kind = OPENRFS_SETTINGS_NOTE;
    copy_text(row.label, sizeof(row.label), "OpenRFS desktop environment");
    (void)openrfs_settings_add_row(0U, &row);
    row.kind = OPENRFS_SETTINGS_CHOICE;
    row.setting = OPENRFS_SET_WIDGET_THEME;
    copy_text(row.label, sizeof(row.label), "Widget theme");
    (void)openrfs_settings_add_row(0U, &row);
    zero_bytes(&row, sizeof(row));
    row.kind = OPENRFS_SETTINGS_SWITCH;
    row.on = true;
    row.setting = OPENRFS_SET_DESKTOP_ICONS;
    copy_text(row.label, sizeof(row.label), "Show desktop icons");
    (void)openrfs_settings_add_row(1U, &row);
    zero_bytes(&row, sizeof(row));
    row.kind = OPENRFS_SETTINGS_NOTE;
    copy_text(row.label, sizeof(row.label), "Minimal bottom panel");
    (void)openrfs_settings_add_row(2U, &row);
}

static void populate_taskmgr(void)
{
    static const char *const names[] = {
        "openrfs-session", "openrfs-files", "openrfs-terminal", "openrfs-network"
    };
    struct openrfs_taskmgr_row row;

    openrfs_taskmgr_reset();
    for (size_t at = 0U; at < sizeof(names) / sizeof(names[0]); ++at) {
        zero_bytes(&row, sizeof(row));
        copy_text(row.command, sizeof(row.command), names[at]);
        copy_text(row.user, sizeof(row.user), "user");
        row.cpu_tenths = (uint32_t)(at + 1U) * 7U;
        row.rss_kib = 1200U + (uint32_t)at * 640U;
        row.pid = (uint32_t)at + 1U;
        (void)openrfs_taskmgr_add(&row);
    }
}

static const char *icon_for_app(enum openrfs_shell_app app)
{
    switch (app) {
    case OPENRFS_APP_FILES:
        return "file-manager";
    case OPENRFS_APP_TERMINAL:
        return "terminal";
    case OPENRFS_APP_TASKMGR:
    case OPENRFS_APP_SETTINGS:
        return "gtk-preferences";
    case OPENRFS_APP_PACKAGES:
        return "gtk-preferences";
    default:
        return "file-manager";
    }
}

static void sync_panel_tasks(void)
{
    const uint32_t focused = openrfs_shell_focused();

    for (uint32_t at = 0U; at < OPENRFS_PANEL_MAX_TASKS; ++at) {
        (void)openrfs_panel_clear_task(at);
    }
    for (uint32_t at = 0U; at < OPENRFS_SHELL_MAX_WINDOWS; ++at) {
        const struct openrfs_window *window = openrfs_shell_window(at);
        struct openrfs_panel_task task;

        if (window == NULL) {
            continue;
        }
        zero_bytes(&task, sizeof(task));
        copy_text(task.label, sizeof(task.label), window->title);
        task.icon = icon_for_app(openrfs_shell_app_of(at));
        task.active = at == focused;
        task.minimised = window->minimised;
        task.desktop = window->desktop;
        (void)openrfs_panel_set_task(at, &task);
    }
}

static void sync_state(void)
{
    enum ui_panel_id previous = state.active_panel;

    if (native_focus >= 0 && native_windows[native_focus].open) {
        state.active_panel = (enum ui_panel_id)(UI_PANEL_NATIVE_0 +
            (uint32_t)native_focus);
    } else if (minimal_desktop_selected) {
        struct ui_rect terminal;

        state.active_panel = minimal_de_terminal_client(&terminal) ?
            UI_PANEL_TERMINAL : UI_PANEL_NONE;
    } else {
        const uint32_t focused = openrfs_shell_focused();

        state.active_panel = focused < OPENRFS_SHELL_MAX_WINDOWS ?
            panel_for_app(openrfs_shell_app_of(focused)) : UI_PANEL_NONE;
    }
    if (previous != state.active_panel) {
        ++state.renders.panel_transitions;
    }
}

static void draw_native_windows(void)
{
    const struct framebuffer_state framebuffer = framebuffer_get_state();
    const uint32_t red_shift = framebuffer.red_position;
    const uint32_t green_shift = framebuffer.green_position;
    const uint32_t blue_shift = framebuffer.blue_position;

    for (uint32_t slot = 0U; slot < UI_NATIVE_WINDOW_COUNT; ++slot) {
        struct native_window_record *record = &native_windows[slot];
        struct openrfs_rect client;

        if (!record->open) {
            continue;
        }
        record->window.active = native_focus == (int32_t)slot;
        openrfs_window_draw(&desktop, &record->window);
        client = openrfs_window_client(&record->window);
        for (uint32_t y = 0U; y < record->height && y < client.height; ++y) {
            for (uint32_t x = 0U; x < record->width && x < client.width; ++x) {
                const uint32_t packed = record->pixels[
                    (size_t)y * (record->stride_bytes / 4U) + x];
                const uint32_t red = (packed >> red_shift) & 0xFFU;
                const uint32_t green = (packed >> green_shift) & 0xFFU;
                const uint32_t blue = (packed >> blue_shift) & 0xFFU;

                openrfs_surface_plot(&desktop, client, client.x + x,
                    client.y + y, red << 16U | green << 8U | blue);
            }
        }
    }
}

static void draw_cursor(void)
{
    const uint32_t x = state.pointer.x < 0 ? 0U : (uint32_t)state.pointer.x;
    const uint32_t y = state.pointer.y < 0 ? 0U : (uint32_t)state.pointer.y;
    const struct openrfs_rect clip = { 0U, 0U, desktop.width, desktop.height };

    if (!state.pointer_present) {
        return;
    }
    for (uint32_t at = 0U; at < 9U; ++at) {
        openrfs_surface_plot(&desktop, clip, x, y + at, 0x000000U);
        openrfs_surface_plot(&desktop, clip, x + 1U, y + at, 0xFFFFFFU);
    }
    for (uint32_t at = 0U; at < 6U; ++at) {
        openrfs_surface_plot(&desktop, clip, x + at, y + at, 0xFFFFFFU);
    }
}

static enum ui_status render_desktop(void)
{
    const struct openrfs_rect whole = { 0U, 0U, desktop.width, desktop.height };

    if (minimal_desktop_selected) {
        minimal_de_draw();
    } else {
        if (openrfs_wallpaper_decode(0U, desktop.pixels,
                (size_t)desktop.width * desktop.height, desktop.width,
                desktop.height, 16U, 8U, 0U) != WALLPAPER_STATUS_OK) {
            openrfs_surface_fill(&desktop, whole, whole, 0x70757AU);
        }
        openrfs_shell_draw_desktop();
        openrfs_shell_draw();
        sync_panel_tasks();
        if (openrfs_panel_draw(whole) != OPENRFS_PANEL_STATUS_OK) {
            return UI_STATUS_SURFACE_FAILURE;
        }
    }
    draw_native_windows();
    if (minimal_desktop_selected) {
        minimal_de_draw_overlays();
    } else {
        openrfs_shell_draw_overlays();
    }
    draw_cursor();
    for (uint32_t y = 0U; y < desktop.height; ++y) {
        for (uint32_t x = 0U; x < desktop.width; ++x) {
            const uint32_t pixel = desktop.pixels[(size_t)y * desktop.width + x];

            converted_row[x] = framebuffer_pack((uint8_t)(pixel >> 16U),
                (uint8_t)(pixel >> 8U), (uint8_t)pixel);
        }
        if (surface_blit(canvas, 0U, y, converted_row, desktop.width, 1U,
                desktop.width * 4U) != SURFACE_STATUS_OK) {
            return UI_STATUS_SURFACE_FAILURE;
        }
    }
    if (state.active_panel == UI_PANEL_TERMINAL) {
        if (minimal_desktop_selected) {
            struct ui_rect client;

            if (minimal_de_terminal_client(&client)) {
                (void)screen_set_viewport((struct surface_rect){ client.x,
                    client.y, client.width, client.height }, true);
            }
        } else {
            const uint32_t focused = openrfs_shell_focused();
            const struct openrfs_window *window = openrfs_shell_window(focused);

            if (window != NULL) {
                const struct openrfs_rect client = openrfs_window_client(window);

                (void)screen_set_viewport((struct surface_rect){ client.x,
                    client.y, client.width, client.height }, true);
            }
        }
    } else {
        (void)screen_set_visible(false);
    }
    ++state.renders.full_draws;
    ++state.renders.damage_rectangles;
    state.renders.pixels_copied += (uint64_t)desktop.width * desktop.height;
    state.renders.glyphs += 1U;
    redraw_pending = false;
    return UI_STATUS_OK;
}

static uint64_t surface_hash(void)
{
    uint64_t hash = UI_FNV_OFFSET;

    for (size_t at = 0U; at < (size_t)canvas->width * canvas->height; ++at) {
        uint32_t value = canvas->pixels[at];

        for (uint32_t byte = 0U; byte < 4U; ++byte) {
            hash ^= (uint8_t)value;
            hash *= UI_FNV_PRIME;
            value >>= 8U;
        }
    }
    return hash;
}

enum ui_status ui_construct(bool pointer_present)
{
    const struct framebuffer_state framebuffer = framebuffer_get_state();
    struct pointer_state pointer;
    uint64_t bytes;
    enum ui_status status;

    if (state.initialized) {
        return UI_STATUS_ALREADY_INITIALIZED;
    }
    if (!framebuffer.active || !screen_is_active()) {
        return UI_STATUS_SCREEN_FAILURE;
    }
    status = ui_layout_build(framebuffer.width, framebuffer.height,
        &state.layout);
    if (status != UI_STATUS_OK) {
        return status;
    }
    canvas = screen_surface();
    if (canvas == NULL || !canvas->active || canvas->width != framebuffer.width ||
            canvas->height != framebuffer.height) {
        return UI_STATUS_SURFACE_FAILURE;
    }
    if (pointer_present &&
            pointer_set_bounds(framebuffer.width, framebuffer.height) !=
                POINTER_STATUS_OK) {
        return UI_STATUS_BAD_CURSOR_HOTSPOT;
    }
    bytes = (uint64_t)canvas->width * canvas->height * sizeof(uint32_t);
    if (heap_allocate(bytes, (void **)&desktop_pixels) != HEAP_STATUS_OK) {
        return UI_STATUS_SURFACE_FAILURE;
    }
    desktop = (struct openrfs_surface){ desktop_pixels,
        canvas->width, canvas->height };
    if (minimal_desktop_selected) {
        if (!minimal_de_construct(desktop_pixels, desktop.width,
                desktop.height)) {
            (void)heap_free(desktop_pixels);
            desktop_pixels = NULL;
            desktop = (struct openrfs_surface){ NULL, 0U, 0U };
            return UI_STATUS_SURFACE_FAILURE;
        }
        wvrm_last_click_valid = false;
        if (account_session_active()) {
            const uint32_t root = trait_files_root();

            if (openrfsfs_drive(OPENRFSFS_VOLUME_SYSTEM).mounted &&
                    trait_files_add(root, "System", true, 0U) >=
                        TRAIT_FILES_MAX_NODES) {
                return UI_STATUS_SURFACE_FAILURE;
            }
            if (openrfsfs_drive(OPENRFSFS_VOLUME_DATA).mounted &&
                    trait_files_add(root, "Data", true, 0U) >=
                        TRAIT_FILES_MAX_NODES) {
                return UI_STATUS_SURFACE_FAILURE;
            }
            trait_files_set_loader(wvrm_files_load);
        }
        trait_files_set_read_only(true);
    } else {
        if (openrfs_panel_attach(&desktop) != OPENRFS_PANEL_STATUS_OK ||
                openrfs_panel_initialize() != OPENRFS_PANEL_STATUS_OK) {
            (void)heap_free(desktop_pixels);
            desktop_pixels = NULL;
            desktop = (struct openrfs_surface){ NULL, 0U, 0U };
            return UI_STATUS_SURFACE_FAILURE;
        }
        openrfs_shell_reset(&desktop);
        openrfs_shell_set_screen((struct openrfs_rect){ 0U, 0U,
            desktop.width, desktop.height });
        openrfs_shell_set_desktop_folder(populate_files());
        populate_menu();
        populate_packages();
        populate_settings();
        populate_taskmgr();
        openrfs_terminal_reset();
        (void)openrfs_panel_set_clock("09:41");
        (void)openrfs_panel_set_volume(65U, false);
        (void)openrfs_panel_set_desktop(0U, 2U);
        (void)openrfs_panel_push_cpu(8U);
        (void)openrfs_panel_push_cpu(14U);
        (void)openrfs_panel_push_cpu(9U);
        (void)openrfs_shell_open(OPENRFS_APP_FILES,
            default_window(OPENRFS_APP_FILES));
    }
    state.initialized = true;
    state.pointer_present = pointer_present;
    state.focus = minimal_desktop_selected ? UI_ELEMENT_NONE :
        UI_ELEMENT_DOCK_FILES;
    state.hover = UI_ELEMENT_NONE;
    state.pressed = UI_ELEMENT_NONE;
    pointer = pointer_get_state();
    state.pointer = (struct ui_point){ (int32_t)pointer.x, (int32_t)pointer.y };
    set_theme();
    sync_state();
    (void)screen_set_deferred_present(true);
    redraw_pending = true;
    return UI_STATUS_OK;
}

bool ui_select_minimal_desktop(void)
{
    if (state.initialized || state.active) {
        return false;
    }
    minimal_desktop_selected = true;
    return true;
}

enum ui_status ui_activate(void)
{
    enum ui_status status;

    if (!state.initialized) {
        return UI_STATUS_NOT_INITIALIZED;
    }
    if (state.active) {
        return UI_STATUS_ALREADY_INITIALIZED;
    }
    state.active = true;
    status = render_desktop();
    if (status != UI_STATUS_OK) {
        state.active = false;
        return status;
    }
    if (surface_present(canvas) != SURFACE_STATUS_OK) {
        state.active = false;
        return UI_STATUS_SURFACE_FAILURE;
    }
    return UI_STATUS_OK;
}

enum ui_status ui_terminal_draw_logo(void)
{
    return UI_STATUS_LOGO_FAILURE;
}

bool ui_is_active(void)
{
    return state.active;
}

void ui_request_redraw(void)
{
    if (state.active) {
        redraw_pending = true;
    }
}

void ui_animation_attach(void)
{
}

bool ui_animation_active(void)
{
    return false;
}

const struct ui_state *ui_get_state(void)
{
    return &state;
}

enum ui_status ui_event_publish(const struct ui_event *event)
{
    size_t next;

    if (event == NULL) {
        return UI_STATUS_NULL_ARGUMENT;
    }
    if (event->type <= UI_EVENT_NONE || event->type >= UI_EVENT_TYPE_COUNT) {
        return UI_STATUS_BAD_EVENT;
    }
    next = (queue_write + 1U) % UI_EVENT_QUEUE_CAPACITY;
    if (next == queue_read) {
        ++state.events.dropped;
        return UI_STATUS_EVENT_QUEUE_FULL;
    }
    if (event->type == UI_EVENT_POINTER_MOVEMENT && queue_write != queue_read) {
        const size_t previous = (queue_write + UI_EVENT_QUEUE_CAPACITY - 1U) %
            UI_EVENT_QUEUE_CAPACITY;

        if (queue[previous].type == UI_EVENT_POINTER_MOVEMENT) {
            queue[previous] = *event;
            ++state.events.coalesced;
            ++state.events.accepted;
            return UI_STATUS_OK;
        }
    }
    queue[queue_write] = *event;
    queue_write = next;
    ++state.events.accepted;
    return UI_STATUS_OK;
}

static void open_focused_application(void)
{
    const enum openrfs_shell_app app = app_for_focus(state.focus);

    if (app < OPENRFS_APP_COUNT) {
        (void)openrfs_shell_open(app, default_window(app));
        native_focus = -1;
        sync_state();
        redraw_pending = true;
    }
}

enum ui_status ui_handle_keyboard(const struct keyboard_event *event)
{
    struct ui_event translated;

    if (event == NULL) {
        return UI_STATUS_NULL_ARGUMENT;
    }
    if (!state.active) {
        return UI_STATUS_NOT_ACTIVE;
    }
    if (native_focus >= 0 && native_windows[native_focus].open &&
            native_windows[native_focus].handler != NULL) {
        const struct ui_native_event native = {
            .type = UI_NATIVE_EVENT_KEY,
            .monotonic_ns = clock_monotonic_ns(),
            .code = event->scancode,
            .value = event->pressed ? 1U : 0U,
            .modifiers = (event->shift ? 1U : 0U) |
                (event->control ? 2U : 0U) | (event->alt ? 4U : 0U)
        };

        native_windows[native_focus].handler((uint32_t)native_focus,
            &native, native_windows[native_focus].context);
        return UI_STATUS_OK;
    }
    if (!event->pressed) {
        return UI_STATUS_OK;
    }
    zero_bytes(&translated, sizeof(translated));
    translated.character = event->character;
    translated.control = event->control;
    if (event->scancode == 0x0FU) {
        translated.type = event->shift ? UI_EVENT_KEYBOARD_FOCUS_PREVIOUS :
            UI_EVENT_KEYBOARD_FOCUS_NEXT;
    } else if (event->scancode == 0x1CU) {
        translated.type = UI_EVENT_KEYBOARD_ACTIVATION;
    } else if (event->scancode == 0x01U ||
            (event->scancode == 0x3EU && event->alt)) {
        translated.type = UI_EVENT_PANEL_CLOSE;
    } else if (event->character != '\0') {
        translated.type = UI_EVENT_TEXT_INPUT;
    } else {
        return UI_STATUS_OK;
    }
    return ui_event_publish(&translated);
}

static int32_t native_at(struct ui_point point)
{
    for (int32_t slot = (int32_t)UI_NATIVE_WINDOW_COUNT - 1;
            slot >= 0; --slot) {
        if (native_windows[slot].open && rect_contains(
                ui_rect_from_openrfs(native_windows[slot].window.frame),
                point.x, point.y)) {
            return slot;
        }
    }
    return -1;
}

static bool dispatch_native_pointer(const struct ui_event *event)
{
    int32_t target = -1;
    struct native_window_record *record;
    struct openrfs_rect client;
    struct ui_native_event native;

    for (uint32_t slot = 0U; slot < UI_NATIVE_WINDOW_COUNT; ++slot) {
        if (native_windows[slot].open && native_windows[slot].capture) {
            target = (int32_t)slot;
            break;
        }
    }
    if (target < 0) {
        target = native_at(event->point);
    }
    if (target < 0) {
        return false;
    }
    record = &native_windows[target];
    client = openrfs_window_client(&record->window);
    if (event->type == UI_EVENT_POINTER_BUTTON_PRESS) {
        native_focus = target;
        sync_state();
    }
    if (record->handler == NULL) {
        return true;
    }
    zero_bytes(&native, sizeof(native));
    native.monotonic_ns = clock_monotonic_ns();
    native.x = event->point.x - (int32_t)client.x;
    native.y = event->point.y - (int32_t)client.y;
    native.type = event->type == UI_EVENT_POINTER_MOVEMENT ?
        UI_NATIVE_EVENT_POINTER_MOVE : UI_NATIVE_EVENT_POINTER_BUTTON;
    native.code = event->button;
    native.value = event->type == UI_EVENT_POINTER_BUTTON_PRESS ? 1U : 0U;
    native.modifiers = event->control ? 2U : 0U;
    record->handler((uint32_t)target, &native, record->context);
    return true;
}

static bool process_one(const struct ui_event *event)
{
    struct openrfs_event translated;

    if (minimal_desktop_selected) {
        struct ui_event wvrm_event = *event;
        const struct ui_event *delivered = &wvrm_event;
        bool changed;
        bool overlay_dispatched = false;

        if (event->type == UI_EVENT_POINTER_BUTTON_PRESS &&
                event->button == UI_POINTER_BUTTON_LEFT) {
            const uint64_t now = clock_monotonic_ns();

            wvrm_event.double_click = wvrm_last_click_valid &&
                now >= wvrm_last_click_ns &&
                now - wvrm_last_click_ns <= UINT64_C(500000000) &&
                event->point.x == wvrm_last_click_point.x &&
                event->point.y == wvrm_last_click_point.y;
            wvrm_last_click_ns = now;
            wvrm_last_click_point = event->point;
            wvrm_last_click_valid = true;
        }

        if (event->type == UI_EVENT_POINTER_MOVEMENT ||
                event->type == UI_EVENT_POINTER_BUTTON_PRESS ||
                event->type == UI_EVENT_POINTER_BUTTON_RELEASE) {
            const struct ui_point old = state.pointer;

            state.pointer = event->point;
            if (event->type == UI_EVENT_POINTER_MOVEMENT &&
                    (old.x != state.pointer.x || old.y != state.pointer.y)) {
                ++state.renders.cursor_moves;
            }
            if (minimal_de_overlay_open()) {
                overlay_dispatched = true;
                if (minimal_de_event(delivered)) {
                    sync_state();
                    return true;
                }
            }
            if (dispatch_native_pointer(event)) {
                return true;
            }
            if (event->type == UI_EVENT_POINTER_BUTTON_PRESS) {
                native_focus = -1;
            }
        }
        if (event->type == UI_EVENT_PANEL_CLOSE && native_focus >= 0 &&
                native_windows[native_focus].open) {
            (void)ui_native_window_close((uint32_t)native_focus);
            sync_state();
            return true;
        }
        if (overlay_dispatched) {
            sync_state();
            return true;
        }
        changed = minimal_de_event(delivered);
        sync_state();
        return changed || event->type == UI_EVENT_POINTER_MOVEMENT;
    }
    if (event->type == UI_EVENT_KEYBOARD_FOCUS_NEXT ||
            event->type == UI_EVENT_KEYBOARD_FOCUS_PREVIOUS) {
        size_t current = state.focus >= UI_ELEMENT_DOCK_FILES &&
            state.focus <= UI_ELEMENT_DOCK_SETTINGS ?
            (size_t)(state.focus - UI_ELEMENT_DOCK_FILES) : 0U;

        current = event->type == UI_EVENT_KEYBOARD_FOCUS_NEXT ?
            (current + 1U) % UI_DOCK_ITEM_COUNT :
            (current + UI_DOCK_ITEM_COUNT - 1U) % UI_DOCK_ITEM_COUNT;
        state.focus = focus_for_index(current);
        return true;
    }
    if (event->type == UI_EVENT_KEYBOARD_ACTIVATION) {
        open_focused_application();
        return true;
    }
    if (event->type == UI_EVENT_PANEL_CLOSE) {
        if (native_focus >= 0 && native_windows[native_focus].open) {
            (void)ui_native_window_close((uint32_t)native_focus);
        } else {
            const uint32_t focused = openrfs_shell_focused();

            if (focused < OPENRFS_SHELL_MAX_WINDOWS) {
                (void)openrfs_shell_close(focused);
            }
        }
        sync_state();
        return true;
    }
    if (event->type == UI_EVENT_TASK_MANAGER) {
        (void)openrfs_shell_open(OPENRFS_APP_TASKMGR,
            default_window(OPENRFS_APP_TASKMGR));
        native_focus = -1;
        sync_state();
        return true;
    }
    if (event->type == UI_EVENT_REDRAW_REQUEST) {
        return true;
    }
    if (event->type == UI_EVENT_POINTER_MOVEMENT ||
            event->type == UI_EVENT_POINTER_BUTTON_PRESS ||
            event->type == UI_EVENT_POINTER_BUTTON_RELEASE) {
        const struct ui_point old = state.pointer;

        state.pointer = event->point;
        if (event->type == UI_EVENT_POINTER_MOVEMENT &&
                (old.x != state.pointer.x || old.y != state.pointer.y)) {
            ++state.renders.cursor_moves;
        }
        if (dispatch_native_pointer(event)) {
            return true;
        }
    }
    zero_bytes(&translated, sizeof(translated));
    translated.x = event->point.x < 0 ? 0U : (uint32_t)event->point.x;
    translated.y = event->point.y < 0 ? 0U : (uint32_t)event->point.y;
    translated.modifiers = event->control ? OPENRFS_MOD_CTRL : 0U;
    translated.key = event->character;
    translated.secondary = event->button == UI_POINTER_BUTTON_RIGHT;
    switch (event->type) {
    case UI_EVENT_POINTER_MOVEMENT:
        translated.kind = OPENRFS_EVENT_POINTER_MOVE;
        break;
    case UI_EVENT_POINTER_BUTTON_PRESS:
        translated.kind = OPENRFS_EVENT_POINTER_DOWN;
        break;
    case UI_EVENT_POINTER_BUTTON_RELEASE:
        translated.kind = OPENRFS_EVENT_POINTER_UP;
        break;
    case UI_EVENT_TEXT_INPUT:
        translated.kind = OPENRFS_EVENT_KEY;
        break;
    default:
        return false;
    }
    if (event->type == UI_EVENT_POINTER_BUTTON_PRESS) {
        native_focus = -1;
    }
    if (openrfs_shell_handle(&translated)) {
        sync_state();
        return true;
    }
    if (event->type == UI_EVENT_POINTER_BUTTON_PRESS) {
        sync_state();
        return true;
    }
    return event->type == UI_EVENT_POINTER_MOVEMENT;
}

enum ui_status ui_process_events(void)
{
    if (!state.active) {
        return UI_STATUS_NOT_ACTIVE;
    }
    while (queue_read != queue_write) {
        const struct ui_event event = queue[queue_read];

        queue_read = (queue_read + 1U) % UI_EVENT_QUEUE_CAPACITY;
        ++state.events.drained;
        if (process_one(&event)) {
            redraw_pending = true;
        }
    }
    return UI_STATUS_OK;
}

enum ui_status ui_flush(void)
{
    enum ui_status status;

    if (!state.active) {
        return UI_STATUS_NOT_ACTIVE;
    }
    if (!redraw_pending) {
        return UI_STATUS_OK;
    }
    status = render_desktop();
    if (status != UI_STATUS_OK) {
        return status;
    }
    return surface_present(canvas) == SURFACE_STATUS_OK ? UI_STATUS_OK :
        UI_STATUS_SURFACE_FAILURE;
}

enum ui_status ui_native_window_open(uint32_t slot, const char *title,
    const uint32_t *pixels, uint32_t width, uint32_t height,
    uint32_t stride_bytes, ui_native_event_fn event_handler, void *context)
{
    struct native_window_record *record;
    uint32_t frame_width;
    uint32_t frame_height;
    uint32_t frame_x;
    uint32_t frame_y;

    if (title == NULL || pixels == NULL) {
        return UI_STATUS_NULL_ARGUMENT;
    }
    if (!state.active) {
        return UI_STATUS_NOT_ACTIVE;
    }
    if (slot >= UI_NATIVE_WINDOW_COUNT || width < 64U || height < 64U ||
            stride_bytes < width * 4U || stride_bytes % 4U != 0U) {
        return UI_STATUS_BAD_PANEL;
    }
    record = &native_windows[slot];
    if (record->open) {
        return UI_STATUS_ALREADY_INITIALIZED;
    }
    frame_width = width + OPENRFS_BORDER * 2U;
    frame_height = height + OPENRFS_TITLE_HEIGHT + OPENRFS_BORDER;
    if (frame_width > desktop.width ||
            frame_height > desktop.height - OPENRFS_PANEL_HEIGHT) {
        return UI_STATUS_UNSUPPORTED_GEOMETRY;
    }
    zero_bytes(record, sizeof(*record));
    record->open = true;
    record->pixels = pixels;
    record->width = width;
    record->height = height;
    record->stride_bytes = stride_bytes;
    record->handler = event_handler;
    record->context = context;
    frame_x = (desktop.width - frame_width) / 2U + slot * 18U;
    frame_y = (desktop.height - OPENRFS_PANEL_HEIGHT - frame_height) / 2U +
        slot * 18U;
    if (frame_x > desktop.width - frame_width) {
        frame_x = desktop.width - frame_width;
    }
    if (frame_y > desktop.height - OPENRFS_PANEL_HEIGHT - frame_height) {
        frame_y = desktop.height - OPENRFS_PANEL_HEIGHT - frame_height;
    }
    record->window.frame = (struct openrfs_rect){ frame_x, frame_y,
        frame_width, frame_height };
    record->window.desktop = openrfs_shell_desktop();
    openrfs_window_set_title(&record->window, title);
    native_focus = (int32_t)slot;
    sync_state();
    redraw_pending = true;
    return UI_STATUS_OK;
}

enum ui_status ui_native_window_close(uint32_t slot)
{
    if (slot >= UI_NATIVE_WINDOW_COUNT || !native_windows[slot].open) {
        return UI_STATUS_BAD_PANEL;
    }
    zero_bytes(&native_windows[slot], sizeof(native_windows[slot]));
    if (native_focus == (int32_t)slot) {
        native_focus = -1;
    }
    sync_state();
    redraw_pending = true;
    return UI_STATUS_OK;
}

enum ui_status ui_native_window_damage(uint32_t slot,
    const struct ui_rect *rectangles, size_t rectangle_count)
{
    if (slot >= UI_NATIVE_WINDOW_COUNT || !native_windows[slot].open ||
            rectangles == NULL || rectangle_count == 0U) {
        return UI_STATUS_NULL_ARGUMENT;
    }
    for (size_t at = 0U; at < rectangle_count; ++at) {
        if (!rect_fits(rectangles[at], (struct ui_rect){ 0U, 0U,
                native_windows[slot].width, native_windows[slot].height })) {
            return UI_STATUS_RECTANGLE_OUT_OF_BOUNDS;
        }
    }
    state.renders.damage_rectangles += rectangle_count;
    redraw_pending = true;
    return UI_STATUS_OK;
}

enum ui_status ui_native_pointer_capture(uint32_t slot, bool capture)
{
    if (slot >= UI_NATIVE_WINDOW_COUNT || !native_windows[slot].open) {
        return UI_STATUS_BAD_PANEL;
    }
    native_windows[slot].capture = capture;
    return UI_STATUS_OK;
}

bool ui_native_window_is_open(uint32_t slot)
{
    return slot < UI_NATIVE_WINDOW_COUNT && native_windows[slot].open;
}

bool ui_application_launch_dequeue(char *manifest_path, size_t capacity)
{
    if (manifest_path != NULL && capacity != 0U) {
        manifest_path[0] = '\0';
    }
    return false;
}

bool ui_self_test(void)
{
    struct ui_layout layout;
    enum ui_element_id hit;

    if (ui_layout_build(1024U, 768U, &layout) != UI_STATUS_OK ||
            ui_layout_validate(&layout) != UI_STATUS_OK ||
            ui_hit_test(&layout, (struct ui_point){
                (int32_t)layout.dock_items[0].bounds.x,
                (int32_t)layout.dock_items[0].bounds.y }, &hit) !=
                UI_STATUS_OK || hit != UI_ELEMENT_DOCK_FILES) {
        self_test_failure = "OpenRFS desktop layout self-test failed";
        return false;
    }
    if (!minimal_de_self_test()) {
        self_test_failure = "minimal desktop source self-test failed";
        return false;
    }
    if (!openrfs_menu_self_test()) {
        self_test_failure = "OpenRFS menu self-test failed";
        return false;
    }
    if (!openrfs_files_self_test()) {
        self_test_failure = "OpenRFS Files self-test failed";
        return false;
    }
    if (!openrfs_packages_self_test()) {
        self_test_failure = "OpenRFS package-manager UI self-test failed";
        return false;
    }
    if (!openrfs_settings_self_test()) {
        self_test_failure = "OpenRFS settings self-test failed";
        return false;
    }
    if (!openrfs_taskmgr_self_test()) {
        self_test_failure = "OpenRFS task-manager self-test failed";
        return false;
    }
    if (!openrfs_terminal_self_test()) {
        self_test_failure = "OpenRFS terminal self-test failed";
        return false;
    }
    if (!openrfs_panel_self_test()) {
        self_test_failure = "OpenRFS panel self-test failed";
        return false;
    }
    if (!openrfs_shell_self_test()) {
        self_test_failure = "OpenRFS shell self-test failed";
        return false;
    }
    self_test_failure = "OpenRFS desktop self-test passed";
    return true;
}

const char *ui_self_test_failure(void)
{
    return self_test_failure;
}

enum ui_status ui_verify_installed(struct ui_proof *proof)
{
    const struct boot_ledger *ledger;
    uint64_t first;
    uint64_t second;

    if (proof == NULL) {
        return UI_STATUS_NULL_ARGUMENT;
    }
    if (!state.active || canvas == NULL || !openrfs_panel_is_initialized() ||
            ui_layout_validate(&state.layout) != UI_STATUS_OK ||
            openrfs_shell_window_count() == 0U || !ui_font_is_verified()) {
        installed_failure = "OpenRFS installed desktop state is incomplete";
        return UI_STATUS_INSTALLED_PROOF_FAILURE;
    }
    redraw_pending = true;
    if (render_desktop() != UI_STATUS_OK) {
        installed_failure = "OpenRFS installed desktop redraw failed";
        return UI_STATUS_INSTALLED_PROOF_FAILURE;
    }
    first = surface_hash();
    redraw_pending = true;
    if (render_desktop() != UI_STATUS_OK) {
        installed_failure = "OpenRFS installed desktop second redraw failed";
        return UI_STATUS_INSTALLED_PROOF_FAILURE;
    }
    second = surface_hash();
    if (first != second || second == 0U) {
        installed_failure = "OpenRFS installed desktop redraw is unstable";
        return UI_STATUS_INSTALLED_PROOF_FAILURE;
    }
    state.stable_render_hash = second;
    state.ledger_pass = true;
    ledger = boot_ledger_installed();
    *proof = (struct ui_proof){
        .width = canvas->width,
        .height = canvas->height,
        .dock_items = UI_DOCK_ITEM_COUNT,
        .events = state.events.drained,
        .panels = state.renders.panel_transitions,
        .cursor_moves = state.renders.cursor_moves,
        .damage_rectangles = state.renders.damage_rectangles,
        .glyphs = state.renders.glyphs,
        .ledger_fingerprint = ledger == NULL ? 0U : ledger->fingerprint,
        .render_hash = second
    };
    installed_failure = "OpenRFS installed desktop proof passed";
    return UI_STATUS_OK;
}

const char *ui_installed_proof_failure(void)
{
    return installed_failure;
}

const char *ui_status_string(enum ui_status status)
{
    switch (status) {
    case UI_STATUS_OK: return "ok";
    case UI_STATUS_NULL_ARGUMENT: return "null argument";
    case UI_STATUS_ALREADY_INITIALIZED: return "already initialized";
    case UI_STATUS_NOT_INITIALIZED: return "not initialized";
    case UI_STATUS_NOT_ACTIVE: return "not active";
    case UI_STATUS_UNSUPPORTED_GEOMETRY: return "unsupported geometry";
    case UI_STATUS_RECTANGLE_OVERFLOW: return "rectangle overflow";
    case UI_STATUS_RECTANGLE_OUT_OF_BOUNDS: return "rectangle out of bounds";
    case UI_STATUS_DUPLICATE_ELEMENT_ID: return "duplicate element";
    case UI_STATUS_DOCK_OVERLAP: return "desktop control overlap";
    case UI_STATUS_EMPTY_PANEL_CLIENT: return "empty window client";
    case UI_STATUS_TEXT_BASELINE_OUT_OF_BOUNDS: return "text out of bounds";
    case UI_STATUS_BAD_CURSOR_HOTSPOT: return "bad cursor hotspot";
    case UI_STATUS_HIT_TEST_AMBIGUOUS: return "ambiguous hit test";
    case UI_STATUS_EVENT_QUEUE_FULL: return "event queue full";
    case UI_STATUS_BAD_EVENT: return "bad event";
    case UI_STATUS_BAD_ELEMENT: return "bad element";
    case UI_STATUS_BAD_PANEL: return "bad window";
    case UI_STATUS_FONT_FAILURE: return "font failure";
    case UI_STATUS_SURFACE_FAILURE: return "surface failure";
    case UI_STATUS_LOGO_FAILURE: return "logo unavailable";
    case UI_STATUS_APP_ICON_FAILURE: return "app icon failure";
    case UI_STATUS_WALLPAPER_FAILURE: return "wallpaper failure";
    case UI_STATUS_FILESYSTEM_FAILURE: return "filesystem failure";
    case UI_STATUS_SCREEN_FAILURE: return "screen failure";
    case UI_STATUS_INSTALLED_PROOF_FAILURE: return "installed proof failure";
    default: return "unknown UI status";
    }
}

const char *ui_panel_name(enum ui_panel_id panel)
{
    static const char *const names[] = {
        "None", "Files", "Terminal", "Task Manager", "Packages",
        "Settings", "Task Manager", "Native application",
        "Native application", "Native application", "Native application"
    };

    return (size_t)panel < sizeof(names) / sizeof(names[0]) ? names[panel] :
        "Unknown window";
}

const char *ui_element_name(enum ui_element_id element)
{
    switch (element) {
    case UI_ELEMENT_NONE: return "none";
    case UI_ELEMENT_DOCK_FILES: return "Files";
    case UI_ELEMENT_DOCK_TERMINAL: return "Terminal";
    case UI_ELEMENT_DOCK_TASKMGR: return "Task Manager";
    case UI_ELEMENT_DOCK_PACKAGES: return "Packages";
    case UI_ELEMENT_DOCK_SETTINGS: return "Settings";
    default: return element < UI_ELEMENT_COUNT ? "desktop control" :
        "unknown control";
    }
}

/* The minimal console renders pending damage without an event queue. */
bool ui_events_pending(void)
{
    hwdrv_poll_input();
    return state.active && redraw_pending;
}
