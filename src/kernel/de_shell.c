/* SPDX-License-Identifier: GPL-3.0-only */
#include <opengat/de/shell.h>

#include <opengat/de/files.h>
#include <opengat/de/font.h>
#include <opengat/de/packages.h>
#include <opengat/de/menu.h>
#include <opengat/de/theme.h>
#include <opengat/de/panel.h>
#include <opengat/de/window.h>
#include <opengat/de/settings.h>
#include <opengat/de/taskmgr.h>
#include <opengat/de/terminal.h>

static struct opengat_surface *canvas;
static struct opengat_rect shell_screen;
static uint32_t shell_desktop;
static bool menu_open;
static bool volume_open;
static uint32_t volume_level = 65U;
static bool volume_muted;

static bool context_open;
static uint32_t context_x;
static uint32_t context_y;
static uint32_t context_node = OPENGAT_FILES_MAX_NODES;

static bool rename_open;
static char rename_text[OPENGAT_FILES_NAME_BYTES];
static uint32_t rename_length;
static char rename_error[48];

#define CONTEXT_ROWS 4U
#define CONTEXT_ROW_H 20U
#define CONTEXT_W 150U

static bool run_open;
static char run_text[48];
static uint32_t run_length;
static char run_error[64];

static struct {
    char title[OPENGAT_SHELL_NOTE_BYTES];
    char body[OPENGAT_SHELL_NOTE_BYTES];
    uint32_t life;
} notes[OPENGAT_SHELL_MAX_NOTES];
static uint32_t note_count;

static char tip_text[48];
static uint32_t tip_rested;
static uint32_t tip_x;
static uint32_t tip_y;

/* Resizing: which edge is being dragged, and the frame it started from. */
static bool resizing;
static uint32_t resize_slot;
static uint32_t resize_edges;
static struct opengat_rect resize_from;
static uint32_t resize_ox;
static uint32_t resize_oy;

#define EDGE_LEFT 0x1U
#define EDGE_RIGHT 0x2U
#define EDGE_TOP 0x4U
#define EDGE_BOTTOM 0x8U
#define RESIZE_GRIP 5U
#define MIN_WINDOW 180U

static bool switcher_open;
static uint32_t switcher_at;

/* The desktop folder, and the two standard marks before it. */
#define DESKTOP_STANDARD 2U
#define DESKTOP_CELL_W 86U
#define DESKTOP_CELL_H 74U
#define DESKTOP_MARGIN 8U
static uint32_t desktop_folder = OPENGAT_FILES_MAX_NODES;
static struct opengat_window windows[OPENGAT_SHELL_MAX_WINDOWS];
static enum opengat_shell_app apps[OPENGAT_SHELL_MAX_WINDOWS];
static bool used[OPENGAT_SHELL_MAX_WINDOWS];

/*
 * THE STACK, TOP LAST.  Drawing walks it forwards and hit-testing walks
 * it backwards, which is the whole of why a click lands on the window you
 * can see rather than the one underneath it.  Holding the order in one
 * array rather than as a z field per window means the two can never
 * disagree.
 */
static uint32_t stack[OPENGAT_SHELL_MAX_WINDOWS];
static uint32_t stack_depth;

/* Where a drag started, and what it is moving. */
static bool dragging;
static uint32_t drag_slot;
static uint32_t drag_dx;
static uint32_t drag_dy;

/* A drag that started on a file-manager entry rather than a title bar:
 * releasing it over a folder moves the thing there. */
static bool dragging_entry;
static uint32_t drag_node;

static const char *const TITLES[OPENGAT_APP_COUNT] = {
    "OpenGAT Files", "user@opengat: ~", "OpenGAT Task Manager",
    "OpenGAT Desktop Settings", "OpenGAT DE Package Manager"
};

static void set_title(struct opengat_window *window, const char *text)
{
    opengat_window_set_title(window, text);
}

static void stack_remove(uint32_t slot)
{
    uint32_t at;

    for (at = 0U; at < stack_depth; ++at) {
        if (stack[at] != slot) {
            continue;
        }
        for (; at + 1U < stack_depth; ++at) {
            stack[at] = stack[at + 1U];
        }
        --stack_depth;
        return;
    }
}

static void stack_raise(uint32_t slot)
{
    stack_remove(slot);
    if (stack_depth < OPENGAT_SHELL_MAX_WINDOWS) {
        stack[stack_depth++] = slot;
    }
}

void opengat_shell_reset(struct opengat_surface *surface)
{
    uint32_t at;

    canvas = surface;
    shell_screen.x = 0U;
    shell_screen.y = 0U;
    shell_screen.width = surface != NULL ? surface->width : 0U;
    shell_screen.height = surface != NULL ? surface->height : 0U;
    shell_desktop = 0U;
    menu_open = false;
    volume_open = false;
    run_open = false;
    run_length = 0U;
    run_text[0] = '\0';
    run_error[0] = '\0';
    switcher_open = false;
    note_count = 0U;
    tip_rested = 0U;
    tip_text[0] = '\0';
    resizing = false;
    context_open = false;
    rename_open = false;
    rename_length = 0U;
    rename_text[0] = '\0';
    rename_error[0] = '\0';
    stack_depth = 0U;
    dragging = false;
    dragging_entry = false;
    for (at = 0U; at < OPENGAT_SHELL_MAX_WINDOWS; ++at) {
        used[at] = false;
    }
}

void opengat_shell_set_screen(struct opengat_rect screen)
{
    shell_screen = screen;
}

void opengat_shell_set_desktop(uint32_t desktop)
{
    uint32_t at;

    shell_desktop = desktop;
    (void)opengat_panel_set_desktop(desktop, 2U);
    /*
     * Focus has to land on this desktop.  Leaving it on a window you
     * can no longer see means the next keystroke goes somewhere
     * invisible, which is the workspace bug everybody has met.
     */
    at = stack_depth;
    while (at != 0U) {
        uint32_t slot = stack[--at];

        if (used[slot] && !windows[slot].minimised &&
                windows[slot].desktop == desktop) {
            opengat_shell_focus(slot);
            return;
        }
    }
    for (at = 0U; at < OPENGAT_SHELL_MAX_WINDOWS; ++at) {
        windows[at].active = false;
    }
}

uint32_t opengat_shell_desktop(void)
{
    return shell_desktop;
}

void opengat_shell_send_to_desktop(uint32_t slot, uint32_t desktop)
{
    if (slot >= OPENGAT_SHELL_MAX_WINDOWS || !used[slot]) {
        return;
    }
    windows[slot].desktop = desktop;
}

static void copy_note(char *out, const char *text)
{
    uint32_t at = 0U;

    while (text != NULL && text[at] != '\0' &&
            at + 1U < OPENGAT_SHELL_NOTE_BYTES) {
        out[at] = text[at];
        ++at;
    }
    out[at] = '\0';
}

void opengat_shell_notify(const char *title, const char *body)
{
    uint32_t at;

    /* Full means the OLDEST goes, not the newest refused: the thing that
     * just happened is the thing worth saying. */
    if (note_count == OPENGAT_SHELL_MAX_NOTES) {
        for (at = 1U; at < OPENGAT_SHELL_MAX_NOTES; ++at) {
            notes[at - 1U] = notes[at];
        }
        --note_count;
    }
    copy_note(notes[note_count].title, title);
    copy_note(notes[note_count].body, body);
    notes[note_count].life = 12U;
    ++note_count;
}

uint32_t opengat_shell_note_count(void)
{
    return note_count;
}

const char *opengat_shell_note_title(uint32_t at)
{
    return at < note_count ? notes[at].title : "";
}

const char *opengat_shell_note_body(uint32_t at)
{
    return at < note_count ? notes[at].body : "";
}

void opengat_shell_tick(void)
{
    uint32_t at = 0U;

    while (at < note_count) {
        if (notes[at].life != 0U) {
            --notes[at].life;
        }
        if (notes[at].life == 0U) {
            uint32_t move;

            for (move = at + 1U; move < note_count; ++move) {
                notes[move - 1U] = notes[move];
            }
            --note_count;
            continue;
        }
        ++at;
    }
    if (tip_text[0] != '\0' && tip_rested < OPENGAT_SHELL_TIP_TICKS) {
        ++tip_rested;
    }
}

bool opengat_shell_tip_visible(void)
{
    return tip_text[0] != '\0' && tip_rested >= OPENGAT_SHELL_TIP_TICKS;
}

const char *opengat_shell_tip_text(void)
{
    return tip_text;
}

struct opengat_rect opengat_shell_tip_bounds(void)
{
    struct opengat_rect box = { 0U, 0U, 0U, 0U };

    if (!opengat_shell_tip_visible()) {
        return box;
    }
    box.width = opengat_font_width(tip_text) + 14U;
    box.height = 20U;
    box.x = tip_x > box.width / 2U ? tip_x - box.width / 2U : 0U;
    if (box.x + box.width > shell_screen.x + shell_screen.width) {
        box.x = shell_screen.x + shell_screen.width - box.width;
    }
    /* ABOVE the pointer, because the bar is at the foot of the screen and
     * a tip below it would be off the display. */
    box.y = tip_y > box.height + 6U ? tip_y - box.height - 6U : 0U;
    return box;
}

bool opengat_shell_menu_open(void)
{
    return menu_open;
}

static const char *const CONTEXT_LABELS[CONTEXT_ROWS] = {
    "Open", "Rename", "Delete", "Properties"
};

bool opengat_shell_context_open(void)
{
    return context_open;
}

uint32_t opengat_shell_context_row_count(void)
{
    return CONTEXT_ROWS;
}

const char *opengat_shell_context_row(uint32_t at)
{
    return at < CONTEXT_ROWS ? CONTEXT_LABELS[at] : "";
}

struct opengat_rect opengat_shell_context_bounds(void)
{
    struct opengat_rect box;

    box.width = CONTEXT_W;
    box.height = CONTEXT_ROWS * CONTEXT_ROW_H + 8U;
    box.x = context_x;
    box.y = context_y;
    /* Kept on the screen: a menu opened near the right edge would run
     * off it, and near the foot would run under the panel. */
    if (box.x + box.width > shell_screen.x + shell_screen.width) {
        box.x = shell_screen.x + shell_screen.width - box.width;
    }
    if (box.y + box.height >
            shell_screen.y + shell_screen.height - OPENGAT_PANEL_HEIGHT) {
        box.y = shell_screen.y + shell_screen.height -
            OPENGAT_PANEL_HEIGHT - box.height;
    }
    return box;
}

uint32_t opengat_shell_context_node(void)
{
    return context_node;
}

bool opengat_shell_rename_open(void)
{
    return rename_open;
}

const char *opengat_shell_rename_text(void)
{
    return rename_text;
}

const char *opengat_shell_rename_error(void)
{
    return rename_error;
}

bool opengat_shell_run_open(void)
{
    return run_open;
}

const char *opengat_shell_run_text(void)
{
    return run_text;
}

const char *opengat_shell_run_error(void)
{
    return run_error;
}

bool opengat_shell_switcher_open(void)
{
    return switcher_open;
}

uint32_t opengat_shell_switcher_at(void)
{
    return switcher_at;
}

/* ------------------------------------------------------ the root window */

void opengat_shell_set_desktop_folder(uint32_t folder)
{
    desktop_folder = folder;
}

uint32_t opengat_shell_desktop_icon_count(void)
{
    if (desktop_folder >= OPENGAT_FILES_MAX_NODES) {
        return DESKTOP_STANDARD;
    }
    return DESKTOP_STANDARD + opengat_files_child_count(desktop_folder);
}

bool opengat_shell_desktop_icon_bounds(uint32_t at, struct opengat_rect *out)
{
    uint32_t rows;

    if (out == NULL || at >= opengat_shell_desktop_icon_count()) {
        return false;
    }
    rows = (shell_screen.height > OPENGAT_PANEL_HEIGHT + DESKTOP_MARGIN) ?
        (shell_screen.height - OPENGAT_PANEL_HEIGHT - DESKTOP_MARGIN) /
            DESKTOP_CELL_H : 1U;
    if (rows == 0U) {
        rows = 1U;
    }
    /* Down the left edge first, then a second column - which is the way
     * every desktop fills, and the reason it is not `at % columns`. */
    out->x = shell_screen.x + DESKTOP_MARGIN +
        (at / rows) * DESKTOP_CELL_W;
    out->y = shell_screen.y + DESKTOP_MARGIN +
        (at % rows) * DESKTOP_CELL_H;
    out->width = DESKTOP_CELL_W;
    out->height = DESKTOP_CELL_H;
    return true;
}

void opengat_shell_draw_desktop(void)
{
    static const char *const STANDARD[DESKTOP_STANDARD] = {
        "user-home", "user-trash"
    };
    static const char *const LABELS[DESKTOP_STANDARD] = {
        "user", "Trash"
    };
    uint32_t at;

    if (!opengat_surface_valid(canvas)) {
        return;
    }
    for (at = 0U; at < opengat_shell_desktop_icon_count(); ++at) {
        struct opengat_rect cell;
        const char *mark;
        const char *label;

        if (!opengat_shell_desktop_icon_bounds(at, &cell)) {
            continue;
        }
        if (at < DESKTOP_STANDARD) {
            mark = STANDARD[at];
            label = LABELS[at];
        } else {
            uint32_t node = opengat_files_child(desktop_folder,
                                              at - DESKTOP_STANDARD);

            if (node >= OPENGAT_FILES_MAX_NODES) {
                continue;
            }
            mark = opengat_files_node_mark(node);
            label = opengat_files_node_name(node);
        }
        opengat_files_draw_icon_at(canvas, cell, mark, 48U,
            cell.x + (cell.width - 48U) / 2U, cell.y + 4U);
        {
            uint32_t width = opengat_font_width(label);

            /*
             * the OpenGAT desktop profile is desktop_fg=#ffffff with
             * desktop_shadow=#000000: white ink over a dark halo, which
             * is what keeps a label readable over a wallpaper that is
             * light in one place and dark in another.  The halo is drawn
             * as the same text offset by one in each direction - cheaper
             * than a blur and what a one-pixel shadow IS.
             */
            uint32_t pen = cell.x + (cell.width > width ?
                (cell.width - width) / 2U : 0U);
            uint32_t base = cell.y + 48U + 16U;

            opengat_font_draw(canvas, cell, pen + 1U, base, label,
                            0x000000U);
            opengat_font_draw(canvas, cell, pen, base + 1U, label,
                            0x000000U);
            opengat_font_draw(canvas, cell, pen, base, label, 0xFFFFFFU);
        }
    }
}

bool opengat_shell_volume_open(void)
{
    return volume_open;
}

uint32_t opengat_shell_volume(void)
{
    return volume_muted ? 0U : volume_level;
}

struct opengat_rect opengat_shell_screen(void)
{
    return shell_screen;
}

uint32_t opengat_shell_open(enum opengat_shell_app app, struct opengat_rect at)
{
    uint32_t slot;

    if ((uint32_t)app >= OPENGAT_APP_COUNT) {
        return OPENGAT_SHELL_MAX_WINDOWS;
    }
    for (slot = 0U; slot < OPENGAT_SHELL_MAX_WINDOWS; ++slot) {
        if (!used[slot]) {
            break;
        }
    }
    if (slot == OPENGAT_SHELL_MAX_WINDOWS) {
        return OPENGAT_SHELL_MAX_WINDOWS;
    }
    used[slot] = true;
    apps[slot] = app;
    windows[slot].frame = at;
    windows[slot].active = false;
    windows[slot].minimised = false;
    windows[slot].maximised = false;
    windows[slot].desktop = shell_desktop;
    set_title(&windows[slot], TITLES[app]);
    stack_raise(slot);
    opengat_shell_focus(slot);
    return slot;
}

bool opengat_shell_close(uint32_t slot)
{
    if (slot >= OPENGAT_SHELL_MAX_WINDOWS || !used[slot]) {
        return false;
    }
    used[slot] = false;
    stack_remove(slot);
    /* Focus falls to whatever is now on top, not to nothing: a desktop
     * with windows open and none focused is a state nobody asked for. */
    if (stack_depth != 0U) {
        opengat_shell_focus(stack[stack_depth - 1U]);
    }
    return true;
}

uint32_t opengat_shell_window_count(void)
{
    return stack_depth;
}

struct opengat_window *opengat_shell_window(uint32_t slot)
{
    if (slot >= OPENGAT_SHELL_MAX_WINDOWS || !used[slot]) {
        return NULL;
    }
    return &windows[slot];
}

enum opengat_shell_app opengat_shell_app_of(uint32_t slot)
{
    if (slot >= OPENGAT_SHELL_MAX_WINDOWS || !used[slot]) {
        return OPENGAT_APP_COUNT;
    }
    return apps[slot];
}

uint32_t opengat_shell_at(uint32_t x, uint32_t y)
{
    uint32_t at = stack_depth;

    /* BACKWARDS: topmost first. */
    while (at != 0U) {
        uint32_t slot = stack[--at];

        if (!windows[slot].minimised &&
                windows[slot].desktop == shell_desktop &&
                opengat_rect_contains(windows[slot].frame, x, y)) {
            return slot;
        }
    }
    return OPENGAT_SHELL_MAX_WINDOWS;
}

uint32_t opengat_shell_focused(void)
{
    uint32_t slot;

    if (stack_depth == 0U) {
        return OPENGAT_SHELL_MAX_WINDOWS;
    }
    slot = stack[stack_depth - 1U];
    if (!used[slot] || !windows[slot].active || windows[slot].minimised ||
            windows[slot].desktop != shell_desktop) {
        return OPENGAT_SHELL_MAX_WINDOWS;
    }
    return slot;
}

void opengat_shell_focus(uint32_t slot)
{
    uint32_t at;

    if (slot >= OPENGAT_SHELL_MAX_WINDOWS || !used[slot]) {
        return;
    }
    stack_raise(slot);
    for (at = 0U; at < OPENGAT_SHELL_MAX_WINDOWS; ++at) {
        windows[at].active = used[at] && at == slot;
    }
}

/* A press near a title-bar button counts as on it: the marks are eight
 * pixels and a pointer is not that accurate, so the box is grown by two
 * on every side.  The DRAWN mark is still the mark - this widens what
 * answers, not what is shown. */
static bool button_box(uint32_t slot, enum opengat_window_button which,
    struct opengat_rect *out)
{
    if (!opengat_window_button_bounds(&windows[slot], which, out)) {
        return false;
    }
    out->x = out->x > 2U ? out->x - 2U : 0U;
    out->y = out->y > 2U ? out->y - 2U : 0U;
    out->width += 4U;
    out->height += 4U;
    return true;
}

/* Maximise fills the work area - the screen above the panel - and never
 * the panel itself, or the bar is under the window that covers it. */
static void toggle_maximise(uint32_t slot, struct opengat_rect screen)
{
    struct opengat_window *window = &windows[slot];

    if (window->maximised) {
        window->frame = window->restore;
        window->maximised = false;
        return;
    }
    window->restore = window->frame;
    window->frame.x = screen.x;
    window->frame.y = screen.y;
    window->frame.width = screen.width;
    window->frame.height = screen.height > OPENGAT_PANEL_HEIGHT ?
        screen.height - OPENGAT_PANEL_HEIGHT : screen.height;
    window->maximised = true;
}

static bool handle_client(uint32_t slot, const struct opengat_event *event)
{
    struct opengat_rect client = opengat_window_client(&windows[slot]);
    uint32_t at;

    switch (apps[slot]) {
    case OPENGAT_APP_TASKMGR: {
        struct opengat_rect box;

        /* A press on a column header sorts by it. */
        for (at = 0U; at < OPENGAT_TASKMGR_COLUMNS; ++at) {
            struct opengat_rect head;

            if (!opengat_taskmgr_header_bounds(&windows[slot],
                    (enum opengat_taskmgr_column)at, &head)) {
                continue;
            }
            if (opengat_rect_contains(head, event->x, event->y)) {
                opengat_taskmgr_sort((enum opengat_taskmgr_column)at);
                return true;
            }
        }
        /* End Task, before the rows: it sits over the list's own area
         * and a press on it must not also pick a row underneath. */
        if (opengat_taskmgr_end_button(&windows[slot], &box) &&
                opengat_rect_contains(box, event->x, event->y)) {
            uint32_t pid = opengat_taskmgr_selected_pid();

            if (!opengat_taskmgr_end_selected()) {
                return false;
            }
            /*
             * THE ROW AND THE WINDOW ARE THE SAME THING.  The Task
             * Manager lists what the shell has open, so ending a row
             * that has a window has to close it - a list that says a
             * process is gone while its window is still on the screen is
             * a list that lies.
             *
             * A WINDOW'S PID STARTS AT 2.  pid 1 is the session, which
             * refuses to be ended; mapping slot 0 to pid 1 made the
             * first window ever opened unkillable for a reason that had
             * nothing to do with it, and it took a failing check to
             * notice because slot 0 is usually something you would not
             * think to end.
             */
            if (pid >= OPENGAT_SHELL_FIRST_PID &&
                    pid - OPENGAT_SHELL_FIRST_PID <
                        OPENGAT_SHELL_MAX_WINDOWS &&
                    used[pid - OPENGAT_SHELL_FIRST_PID]) {
                (void)opengat_shell_close(pid - OPENGAT_SHELL_FIRST_PID);
            }
            opengat_shell_notify("Task Manager", "Task ended");
            return true;
        }
        for (at = 0U; at < OPENGAT_TASKMGR_MAX_ROWS; ++at) {
            struct opengat_rect row;

            if (!opengat_taskmgr_row_bounds(&windows[slot], at, &row)) {
                break;
            }
            if (opengat_rect_contains(row, event->x, event->y)) {
                opengat_taskmgr_select(at);
                return true;
            }
        }
        return false;
    }
    case OPENGAT_APP_SETTINGS:
        for (at = 0U; at < opengat_settings_page_count(); ++at) {
            struct opengat_rect tab;

            if (!opengat_settings_tab_bounds(&windows[slot], at, &tab)) {
                continue;
            }
            if (opengat_rect_contains(tab, event->x, event->y)) {
                opengat_settings_select(at);
                return true;
            }
        }
        /* And the rows on the page you are looking at. */
        for (at = 0U; at < OPENGAT_SETTINGS_MAX_ROWS; ++at) {
            struct opengat_rect row;

            if (!opengat_settings_row_bounds(&windows[slot], at, &row)) {
                break;
            }
            if (opengat_rect_contains(row, event->x, event->y)) {
                return opengat_settings_press(opengat_settings_selected(),
                                            at);
            }
        }
        return false;
    case OPENGAT_APP_FILES:
        for (at = 0U; at < opengat_files_child_count(opengat_files_here());
                ++at) {
            struct opengat_rect cell;
            uint32_t node;

            if (!opengat_files_entry_bounds(&windows[slot], at, &cell)) {
                continue;
            }
            if (!opengat_rect_contains(cell, event->x, event->y)) {
                continue;
            }
            node = opengat_files_child(opengat_files_here(), at);
            if (event->secondary) {
                /* pcmanfm selects what you right-clicked before opening
                 * the menu, so the menu is unambiguously about it. */
                opengat_files_select(node, false);
                context_open = true;
                context_node = node;
                context_x = event->x;
                context_y = event->y;
                return true;
            }
            dragging_entry = true;
            drag_node = node;
            if (event->double_click) {
                /* Opening a FILE is not opening a folder, and pretending
                 * it is would be the file manager lying about what it
                 * did.  Only a folder opens. */
                (void)opengat_files_open(node);
                return true;
            }
            opengat_files_select(node,
                (event->modifiers & OPENGAT_MOD_CTRL) != 0U);
            return true;
        }
        if (opengat_rect_contains(client, event->x, event->y)) {
            opengat_files_clear_selection();
            return true;
        }
        return false;
    case OPENGAT_APP_PACKAGES:
        for (at = 0U; at < opengat_packages_count(); ++at) {
            struct opengat_rect row;

            row.x = client.x;
            row.y = client.y + 30U + at * 19U;
            row.width = client.width;
            row.height = 19U;
            if (opengat_rect_contains(row, event->x, event->y)) {
                opengat_packages_select(at);
                return true;
            }
        }
        return false;
    case OPENGAT_APP_TERMINAL:
    default:
        return false;
    }
}

/*
 * What a press on the bar MEANS.  The panel reports what was hit; this is
 * the only place that knows a launcher opens an application and a task
 * button belongs to a window, because it is the only place that knows
 * windows exist.
 */
static const enum opengat_shell_app LAUNCHER_APPS[3] = {
    OPENGAT_APP_FILES, OPENGAT_APP_PACKAGES, OPENGAT_APP_TERMINAL
};

/* Where the volume slider sits: above the icon, the way lxpanel's does. */
static struct opengat_rect shell_volume_bounds(void)
{
    struct opengat_rect box = { 0U, 0U, 0U, 0U };
    struct opengat_rect icon;

    if (opengat_panel_plugin_bounds(shell_screen, OPENGAT_PANEL_PLUGIN_VOLUME,
            &icon) != OPENGAT_PANEL_STATUS_OK) {
        return box;
    }
    box.width = 26U;
    box.height = 120U;
    box.x = icon.x + (icon.width > box.width ?
        (icon.width - box.width) / 2U : 0U);
    box.y = icon.y > box.height ? icon.y - box.height : 0U;
    return box;
}

/* Which row of the open menu a y coordinate is on.  Rules are shorter
 * than rows, so this walks them rather than dividing. */
static uint32_t shell_menu_row(struct opengat_rect box, uint32_t y)
{
    uint32_t top = box.y + 4U;
    uint32_t at;

    for (at = 0U; at < opengat_menu_row_count(); ++at) {
        uint32_t height = opengat_menu_row_is_rule(at) ? 7U : 20U;

        if (y >= top && y < top + height) {
            return at;
        }
        top += height;
    }
    return opengat_menu_row_count();
}

static bool shell_menu_pick(uint32_t row)
{
    struct opengat_rect where = { 240U, 180U, 560U, 360U };
    const char *label = opengat_menu_row_label(row);

    if (label == NULL) {
        return false;
    }
    /* The menu carries names, not slots: matching on the name means a
     * menu built from what is installed cannot pick the wrong thing when
     * its rows move. */
    if (label[0] == 'L') {           /* Leafpad */
        return opengat_shell_open(OPENGAT_APP_SETTINGS, where) <
            OPENGAT_SHELL_MAX_WINDOWS;
    }
    if (label[0] == 'G') {           /* Galculator */
        return opengat_shell_open(OPENGAT_APP_TASKMGR, where) <
            OPENGAT_SHELL_MAX_WINDOWS;
    }
    if (label[0] == 'S') {           /* System Tools */
        return opengat_shell_open(OPENGAT_APP_PACKAGES, where) <
            OPENGAT_SHELL_MAX_WINDOWS;
    }
    if (label[0] == 'A') {           /* Accessories / Archiver */
        return opengat_shell_open(OPENGAT_APP_FILES, where) <
            OPENGAT_SHELL_MAX_WINDOWS;
    }
    if (label[0] == 'R') {           /* Run... */
        run_open = true;
        run_length = 0U;
        run_text[0] = '\0';
        run_error[0] = '\0';
        return true;
    }
    return false;
}

/*
 * What the Run box runs.  The names are the ones this desktop HAS; a name
 * it does not have is refused out loud rather than opening something
 * else or quietly doing nothing.
 */
/*
 * ALT+TAB WALKS THE STACK, TOP FIRST - most recently used, not slot
 * order.  That ordering IS the feature: index 0 is the window you are on
 * and index 1 is the one you were on before it, which is why tapping
 * Alt+Tab once takes you back to what you were just doing.  Enumerating
 * by slot makes "one back" whatever happened to be created second, and
 * the harness caught exactly that.
 */
static uint32_t switcher_list(uint32_t *out, uint32_t capacity)
{
    uint32_t count = 0U;
    uint32_t at = stack_depth;

    while (at != 0U && count < capacity) {
        uint32_t slot = stack[--at];

        if (used[slot] && windows[slot].desktop == shell_desktop) {
            out[count++] = slot;
        }
    }
    return count;
}

/*
 * WHICH EDGES A POINT IS ON.  Openbox resizes from the border, so this
 * asks how close the point is to each edge of the frame and returns a
 * SET: a corner is two edges, which is what makes a corner drag change
 * both dimensions at once.  Returning a single edge is why some window
 * managers make you drag twice to resize diagonally.
 */
static uint32_t edges_at(uint32_t slot, uint32_t x, uint32_t y)
{
    struct opengat_rect frame = windows[slot].frame;
    uint32_t edges = 0U;

    if (!opengat_rect_contains(frame, x, y)) {
        return 0U;
    }
    if (x < frame.x + RESIZE_GRIP) {
        edges |= EDGE_LEFT;
    }
    if (x + RESIZE_GRIP >= frame.x + frame.width) {
        edges |= EDGE_RIGHT;
    }
    if (y < frame.y + RESIZE_GRIP) {
        edges |= EDGE_TOP;
    }
    if (y + RESIZE_GRIP >= frame.y + frame.height) {
        edges |= EDGE_BOTTOM;
    }
    return edges;
}

/*
 * A resize, applied from the frame the drag STARTED on rather than the
 * one it last had.  Accumulating deltas frame by frame drifts, and it
 * drifts worst when the window hits its minimum size and the pointer
 * carries on - the window then grows from the wrong place on the way
 * back.
 */
static void resize_to(uint32_t x, uint32_t y)
{
    struct opengat_window *window = &windows[resize_slot];
    struct opengat_rect frame = resize_from;
    int32_t dx = (int32_t)x - (int32_t)resize_ox;
    int32_t dy = (int32_t)y - (int32_t)resize_oy;

    if ((resize_edges & EDGE_RIGHT) != 0U) {
        int32_t width = (int32_t)frame.width + dx;

        frame.width = width < (int32_t)MIN_WINDOW ? MIN_WINDOW :
            (uint32_t)width;
    }
    if ((resize_edges & EDGE_BOTTOM) != 0U) {
        int32_t height = (int32_t)frame.height + dy;

        frame.height = height < (int32_t)MIN_WINDOW ? MIN_WINDOW :
            (uint32_t)height;
    }
    if ((resize_edges & EDGE_LEFT) != 0U) {
        int32_t left = (int32_t)frame.x + dx;
        int32_t width = (int32_t)frame.width - dx;

        if (width >= (int32_t)MIN_WINDOW && left >= 0) {
            frame.x = (uint32_t)left;
            frame.width = (uint32_t)width;
        }
    }
    if ((resize_edges & EDGE_TOP) != 0U) {
        int32_t top = (int32_t)frame.y + dy;
        int32_t height = (int32_t)frame.height - dy;

        if (height >= (int32_t)MIN_WINDOW && top >= 0) {
            frame.y = (uint32_t)top;
            frame.height = (uint32_t)height;
        }
    }
    window->frame = frame;
}

/*
 * What the context menu's rows DO.  Open and Delete act at once; Rename
 * puts up a box, because a rename needs a name and there is nowhere else
 * to type one.  Properties is not built, so it does nothing and says so
 * by refusing rather than closing as though it had.
 */
static bool shell_context_pick(uint32_t row)
{
    if (context_node >= OPENGAT_FILES_MAX_NODES) {
        return false;
    }
    switch (row) {
    case 0U:     /* Open */
        return opengat_files_open(context_node);
    case 1U: {   /* Rename */
        const char *name = opengat_files_node_name(context_node);
        uint32_t at = 0U;

        rename_open = true;
        rename_error[0] = '\0';
        /* Prefilled with the current name, because renaming is usually
         * changing part of a name rather than writing a new one. */
        while (name[at] != '\0' && at + 1U < sizeof(rename_text)) {
            rename_text[at] = name[at];
            ++at;
        }
        rename_text[at] = '\0';
        rename_length = at;
        return true;
    }
    case 2U: {   /* Delete */
        char body[OPENGAT_SHELL_NOTE_BYTES];
        const char *name = opengat_files_node_name(context_node);
        uint32_t at = 0U;

        while (name[at] != '\0' && at + 12U < sizeof(body)) {
            body[at] = name[at];
            ++at;
        }
        body[at] = '\0';
        if (!opengat_files_remove(context_node)) {
            opengat_shell_notify("Files", "That cannot be deleted");
            return true;
        }
        {
            static const char TAIL[] = " deleted";
            uint32_t from = 0U;

            while (TAIL[from] != '\0' && at + 1U < sizeof(body)) {
                body[at++] = TAIL[from++];
            }
            body[at] = '\0';
        }
        opengat_shell_notify("Files", body);
        return true;
    }
    default:
        return false;
    }
}

static bool shell_rename_go(void)
{
    if (!opengat_files_rename(context_node, rename_text)) {
        static const char REFUSED[] = "That name is taken or not a name";
        uint32_t at = 0U;

        while (REFUSED[at] != '\0' && at + 1U < sizeof(rename_error)) {
            rename_error[at] = REFUSED[at];
            ++at;
        }
        rename_error[at] = '\0';
        /* Stays OPEN: closing on a name it would not take looks exactly
         * like having renamed it. */
        return true;
    }
    rename_open = false;
    rename_error[0] = '\0';
    return true;
}

static bool shell_run_go(void)
{
    static const struct {
        const char *name;
        enum opengat_shell_app app;
    } RUNNABLE[5] = {
        { "pcmanfm", OPENGAT_APP_FILES },
        { "lxterminal", OPENGAT_APP_TERMINAL },
        { "lxtask", OPENGAT_APP_TASKMGR },
        { "lxappearance", OPENGAT_APP_SETTINGS },
        { "packages", OPENGAT_APP_PACKAGES }
    };
    struct opengat_rect where = { 260U, 200U, 560U, 360U };
    uint32_t at;
    uint32_t byte;

    for (at = 0U; at < 5U; ++at) {
        byte = 0U;
        while (RUNNABLE[at].name[byte] != '\0' &&
                run_text[byte] == RUNNABLE[at].name[byte]) {
            ++byte;
        }
        if (RUNNABLE[at].name[byte] == '\0' && run_text[byte] == '\0') {
            run_open = false;
            run_length = 0U;
            run_text[0] = '\0';
            run_error[0] = '\0';
            return opengat_shell_open(RUNNABLE[at].app, where) <
                OPENGAT_SHELL_MAX_WINDOWS;
        }
    }
    /* Stays OPEN and says why, because closing on a name it could not
     * run would look exactly like having run it. */
    byte = 0U;
    while (run_text[byte] != '\0' && byte + 20U < sizeof(run_error)) {
        run_error[byte] = run_text[byte];
        ++byte;
    }
    run_error[byte] = '\0';
    {
        static const char TAIL[] = ": no such program";
        uint32_t from = 0U;

        while (TAIL[from] != '\0' && byte + 1U < sizeof(run_error)) {
            run_error[byte++] = TAIL[from++];
        }
        run_error[byte] = '\0';
    }
    return true;
}

static bool shell_panel_press(struct opengat_panel_hit hit)
{
    struct opengat_rect where = { 220U, 160U, 560U, 360U };

    switch (hit.kind) {
    case OPENGAT_PANEL_HIT_LAUNCHER:
        if (hit.index >= 3U) {
            return false;
        }
        return opengat_shell_open(LAUNCHER_APPS[hit.index], where) <
            OPENGAT_SHELL_MAX_WINDOWS;
    case OPENGAT_PANEL_HIT_TASK:
        if (hit.index >= OPENGAT_SHELL_MAX_WINDOWS || !used[hit.index]) {
            return false;
        }
        /* Pressing the button of the window that already has focus
         * MINIMISES it, which is what a taskbar does - otherwise the
         * button has nothing to say for the focused window. */
        if (opengat_shell_focused() == hit.index &&
                !windows[hit.index].minimised) {
            windows[hit.index].minimised = true;
            return true;
        }
        windows[hit.index].minimised = false;
        opengat_shell_focus(hit.index);
        return true;
    case OPENGAT_PANEL_HIT_PAGER:
        if (hit.index >= 2U) {
            return false;
        }
        opengat_shell_set_desktop(hit.index);
        return true;
    case OPENGAT_PANEL_HIT_WINCMD: {
        /* Show the desktop: minimise everything, or put it all back if
         * everything is already down. */
        bool any_up = false;
        uint32_t at;

        for (at = 0U; at < OPENGAT_SHELL_MAX_WINDOWS; ++at) {
            if (used[at] && !windows[at].minimised) {
                any_up = true;
            }
        }
        for (at = 0U; at < OPENGAT_SHELL_MAX_WINDOWS; ++at) {
            if (used[at]) {
                windows[at].minimised = any_up;
            }
        }
        return true;
    }
    case OPENGAT_PANEL_HIT_MENU:
        /* A second press on the button that opened it CLOSES it, which
         * is what every menu button does and the thing that is missing
         * when a menu can only be dismissed by clicking away. */
        menu_open = !menu_open;
        volume_open = false;
        return true;
    case OPENGAT_PANEL_HIT_VOLUME:
        volume_open = !volume_open;
        menu_open = false;
        return true;
    case OPENGAT_PANEL_HIT_CLOCK:
    case OPENGAT_PANEL_HIT_NONE:
    default:
        /* Reported so the press does not fall through to a window
         * underneath.  The clock opens a calendar in lxpanel and there
         * is no calendar here, so it does nothing rather than pretending
         * to. */
        return false;
    }
}

bool opengat_shell_handle(const struct opengat_event *event)
{
    uint32_t slot;
    struct opengat_rect title;
    struct opengat_rect close;

    if (event == NULL) {
        return false;
    }
    if (event->kind == OPENGAT_EVENT_KEY) {
        /*
         * THE RUN BOX TAKES THE KEYBOARD while it is open, which is what
         * a modal dialog IS.  Without this, typing into it would also
         * reach the window behind it - the bug that makes a dialog feel
         * like a picture stuck to the screen.
         */
        if (rename_open) {
            if (event->special == OPENGAT_KEY_ESCAPE) {
                rename_open = false;
                return true;
            }
            if (event->special == OPENGAT_KEY_BACKSPACE) {
                if (rename_length != 0U) {
                    rename_text[--rename_length] = '\0';
                }
                return true;
            }
            if (event->special == OPENGAT_KEY_ENTER) {
                return shell_rename_go();
            }
            if (event->key >= 32 && event->key <= 126 &&
                    rename_length + 1U < sizeof(rename_text)) {
                rename_text[rename_length++] = event->key;
                rename_text[rename_length] = '\0';
                rename_error[0] = '\0';
                return true;
            }
            return false;
        }
        if (run_open) {
            if (event->special == OPENGAT_KEY_ESCAPE) {
                run_open = false;
                return true;
            }
            if (event->special == OPENGAT_KEY_BACKSPACE) {
                if (run_length != 0U) {
                    run_text[--run_length] = '\0';
                }
                return true;
            }
            if (event->special == OPENGAT_KEY_ENTER) {
                return shell_run_go();
            }
            if (event->key >= 32 && event->key <= 126 &&
                    run_length + 1U < sizeof(run_text)) {
                run_text[run_length++] = event->key;
                run_text[run_length] = '\0';
                run_error[0] = '\0';
                return true;
            }
            return false;
        }
        /*
         * ALT+TAB.  It is held open while Alt is down, so the state
         * lives here rather than being a one-shot: releasing Alt is what
         * commits the choice, which is how the real one works and why
         * tabbing twice goes two windows back rather than one.
         */
        if (event->special == OPENGAT_KEY_TAB &&
                (event->modifiers & OPENGAT_MOD_ALT) != 0U) {
            uint32_t order[OPENGAT_SHELL_MAX_WINDOWS];
            uint32_t live = switcher_list(order, OPENGAT_SHELL_MAX_WINDOWS);

            if (live == 0U) {
                return false;
            }
            if (!switcher_open) {
                switcher_open = true;
                switcher_at = live > 1U ? 1U : 0U;
            } else {
                switcher_at = (switcher_at + 1U) % live;
            }
            return true;
        }
        if (switcher_open && event->special == 0U && event->key == 0 &&
                (event->modifiers & OPENGAT_MOD_ALT) == 0U) {
            /* Alt came up: commit to whatever is under the marker. */
            uint32_t order[OPENGAT_SHELL_MAX_WINDOWS];
            uint32_t live = switcher_list(order, OPENGAT_SHELL_MAX_WINDOWS);

            switcher_open = false;
            if (switcher_at < live) {
                windows[order[switcher_at]].minimised = false;
                opengat_shell_focus(order[switcher_at]);
            }
            return true;
        }
        slot = opengat_shell_focused();

        if (slot >= OPENGAT_SHELL_MAX_WINDOWS) {
            return false;
        }
        /* A-F4 closes the FOCUSED window, which is the one the keyboard
         * is talking to - not the one under the pointer. */
        if (event->special == OPENGAT_KEY_F4 &&
                (event->modifiers & OPENGAT_MOD_ALT) != 0U) {
            return opengat_shell_close(slot);
        }
        /*
         * Ctrl+X/C/V reach the FOCUSED file manager.  They are handled
         * here rather than in files.c because the clipboard is a
         * desktop-wide thing: copy in one window, paste in another.
         */
        if (apps[slot] == OPENGAT_APP_FILES &&
                (event->modifiers & OPENGAT_MOD_CTRL) != 0U) {
            if (event->key == 'c' || event->key == 'x') {
                return opengat_files_copy_selection(event->key == 'x');
            }
            if (event->key == 'v') {
                uint32_t moved =
                    opengat_files_paste_into(opengat_files_here());

                if (moved == 0U) {
                    return false;
                }
                opengat_shell_notify("Files",
                    moved == 1U ? "1 item pasted" : "items pasted");
                return true;
            }
            if (event->key == 'a') {
                opengat_files_select_all();
                return true;
            }
        }
        if (apps[slot] == OPENGAT_APP_TERMINAL) {
            if (event->special == OPENGAT_KEY_ENTER) {
                opengat_terminal_enter();
                return true;
            }
            if (event->special == OPENGAT_KEY_BACKSPACE) {
                opengat_terminal_backspace();
                return true;
            }
            if (event->key != 0) {
                opengat_terminal_type(event->key);
                return true;
            }
        }
        return false;
    }

    if (event->kind == OPENGAT_EVENT_POINTER_MOVE) {
        if (resizing) {
            resize_to(event->x, event->y);
            return true;
        }
        if (!dragging) {
            /*
             * Not dragging: this is a hover.  The tip RESETS when the
             * pointer moves to something else and counts up while it
             * rests - which is what makes it a tip rather than something
             * that flashes as the mouse crosses the bar.
             */
            struct opengat_panel_hit over =
                opengat_panel_hit(shell_screen, event->x, event->y);
            const char *label = "";

            switch (over.kind) {
            case OPENGAT_PANEL_HIT_MENU:
                label = "Applications";
                break;
            case OPENGAT_PANEL_HIT_LAUNCHER:
                label = over.index == 0U ? "File Manager" :
                    (over.index == 1U ? "Package Manager" : "Terminal");
                break;
            case OPENGAT_PANEL_HIT_WINCMD:
                label = "Show the desktop";
                break;
            case OPENGAT_PANEL_HIT_PAGER:
                label = "Workspace";
                break;
            case OPENGAT_PANEL_HIT_VOLUME:
                label = "Volume";
                break;
            case OPENGAT_PANEL_HIT_CLOCK:
                label = "Clock";
                break;
            default:
                label = "";
                break;
            }
            {
                uint32_t at = 0U;
                bool same_tip = true;

                while (label[at] != '\0' || tip_text[at] != '\0') {
                    if (label[at] != tip_text[at]) {
                        same_tip = false;
                        break;
                    }
                    ++at;
                }
                tip_x = event->x;
                tip_y = event->y;
                if (!same_tip) {
                    at = 0U;
                    while (label[at] != '\0' &&
                            at + 1U < sizeof(tip_text)) {
                        tip_text[at] = label[at];
                        ++at;
                    }
                    tip_text[at] = '\0';
                    tip_rested = 0U;
                    return true;
                }
            }
            return false;
        }
        windows[drag_slot].frame.x = event->x > drag_dx ?
            event->x - drag_dx : 0U;
        windows[drag_slot].frame.y = event->y > drag_dy ?
            event->y - drag_dy : 0U;
        return true;
    }

    if (event->kind == OPENGAT_EVENT_POINTER_UP) {
        bool was = dragging || resizing;

        dragging = false;
        resizing = false;
        if (dragging_entry) {
            uint32_t over = opengat_shell_at(event->x, event->y);

            dragging_entry = false;
            if (over < OPENGAT_SHELL_MAX_WINDOWS &&
                    apps[over] == OPENGAT_APP_FILES) {
                uint32_t at;

                for (at = 0U;
                        at < opengat_files_child_count(opengat_files_here());
                        ++at) {
                    struct opengat_rect cell;
                    uint32_t target;

                    if (!opengat_files_entry_bounds(&windows[over], at,
                                                  &cell)) {
                        continue;
                    }
                    if (!opengat_rect_contains(cell, event->x, event->y)) {
                        continue;
                    }
                    target = opengat_files_child(opengat_files_here(), at);
                    /* opengat_files_move() refuses every bad case itself -
                     * onto a file, onto its own folder, into itself - so
                     * this does not have to know which they are. */
                    return opengat_files_move(drag_node, target);
                }
            }
        }
        return was;
    }

    /*
     * AN OPEN POPUP IS ABOVE EVERYTHING, including the bar that opened
     * it, so it is asked before anything else.  A press inside it does
     * its thing; a press anywhere else DISMISSES it and is then handled
     * normally - which is what makes clicking away from a menu feel like
     * clicking on the thing you clicked on.
     */
    if (context_open) {
        struct opengat_rect box = opengat_shell_context_bounds();

        if (opengat_rect_contains(box, event->x, event->y)) {
            uint32_t row = (event->y - box.y - 4U) / CONTEXT_ROW_H;

            context_open = false;
            return shell_context_pick(row);
        }
        context_open = false;
        /* fall through, so the press still lands where it landed */
    }
    if (menu_open) {
        struct opengat_rect button;
        struct opengat_rect box;

        if (opengat_panel_plugin_bounds(shell_screen,
                OPENGAT_PANEL_PLUGIN_MENU, &button) ==
                OPENGAT_PANEL_STATUS_OK) {
            box = opengat_menu_bounds(shell_screen, button);
            if (opengat_rect_contains(box, event->x, event->y)) {
                uint32_t row = shell_menu_row(box, event->y);

                menu_open = false;
                return shell_menu_pick(row);
            }
            /*
             * A press on the BUTTON is not a dismiss - it is the toggle,
             * and the panel below handles it.  Closing it here as well
             * makes the press close and re-open in one go, which is a
             * menu button that does nothing: the first version of this
             * did exactly that and the harness caught it.
             */
            if (opengat_rect_contains(button, event->x, event->y)) {
                return shell_panel_press((struct opengat_panel_hit){
                    OPENGAT_PANEL_HIT_MENU, 0U });
            }
        }
        menu_open = false;
        /* fall through: the press still lands where it landed */
    }
    if (volume_open) {
        struct opengat_rect slider = shell_volume_bounds();
        struct opengat_rect icon;

        /* The same rule as the menu: a press on the icon is the toggle. */
        if (opengat_panel_plugin_bounds(shell_screen,
                OPENGAT_PANEL_PLUGIN_VOLUME, &icon) ==
                OPENGAT_PANEL_STATUS_OK &&
                opengat_rect_contains(icon, event->x, event->y)) {
            return shell_panel_press((struct opengat_panel_hit){
                OPENGAT_PANEL_HIT_VOLUME, 0U });
        }
        if (opengat_rect_contains(slider, event->x, event->y)) {
            /* The slider runs bottom to top, so a press near its foot is
             * quiet and near its head is loud. */
            uint32_t from_top = event->y - slider.y;

            volume_level = slider.height > 0U ?
                100U - (from_top * 100U / slider.height) : 0U;
            volume_muted = volume_level == 0U;
            (void)opengat_panel_set_volume(volume_level, volume_muted);
            return true;
        }
        volume_open = false;
    }

    /*
     * THE PANEL IS ALWAYS ON TOP, so it is asked first - before the
     * window stack.  A maximised window ends at the bar's top edge, but
     * a window dragged over it would otherwise swallow presses meant for
     * the bar, and a taskbar you cannot click is the worst version of a
     * control that does not do what it is drawn as.
     */
    {
        struct opengat_panel_hit hit =
            opengat_panel_hit(shell_screen, event->x, event->y);

        if (hit.kind != OPENGAT_PANEL_HIT_NONE) {
            return shell_panel_press(hit);
        }
    }

    slot = opengat_shell_at(event->x, event->y);
    if (slot >= OPENGAT_SHELL_MAX_WINDOWS) {
        return false;
    }
    /* Whatever else the press does, it RAISES: that is what clicking a
     * window means, and doing it before anything else means the rest of
     * this function is always talking about the window on top. */
    opengat_shell_focus(slot);

    if (button_box(slot, OPENGAT_WINDOW_CLOSE, &close) &&
            opengat_rect_contains(close, event->x, event->y)) {
        return opengat_shell_close(slot);
    }
    if (button_box(slot, OPENGAT_WINDOW_MAXIMISE, &close) &&
            opengat_rect_contains(close, event->x, event->y)) {
        toggle_maximise(slot, shell_screen);
        return true;
    }
    if (button_box(slot, OPENGAT_WINDOW_MINIMISE, &close) &&
            opengat_rect_contains(close, event->x, event->y)) {
        windows[slot].minimised = true;
        /* Focus goes to whatever is now the top VISIBLE window, not to
         * the one that just went away. */
        {
            uint32_t at = stack_depth;

            while (at != 0U) {
                uint32_t under = stack[--at];

                if (used[under] && !windows[under].minimised) {
                    opengat_shell_focus(under);
                    break;
                }
            }
        }
        return true;
    }
    /*
     * THE BORDER RESIZES, and it is tested before the title bar: the top
     * corners are both, and a window whose top-left corner moves the
     * window instead of resizing it has no way to be made shorter from
     * the top.
     */
    {
        uint32_t edges = edges_at(slot, event->x, event->y);

        if (edges != 0U && !windows[slot].maximised) {
            resizing = true;
            resize_slot = slot;
            resize_edges = edges;
            resize_from = windows[slot].frame;
            resize_ox = event->x;
            resize_oy = event->y;
            return true;
        }
    }
    title = opengat_window_title(&windows[slot]);
    if (opengat_rect_contains(title, event->x, event->y)) {
        dragging = true;
        drag_slot = slot;
        drag_dx = event->x - windows[slot].frame.x;
        drag_dy = event->y - windows[slot].frame.y;
        return true;
    }
    (void)handle_client(slot, event);
    return true;
}

/*
 * THE BAR'S TASK LIST IS THE WINDOW LIST.  Anything that opens, closes or
 * minimises a window calls this, so the buttons on the bar are the
 * windows that exist rather than a list somebody remembered to update.
 * A taskbar carrying a button for a window that closed is the same bug as
 * a button that does nothing, wearing a different coat.
 */
static const char *const APP_ICONS[OPENGAT_APP_COUNT] = {
    "file-manager", "terminal", "gtk-preferences", "gtk-preferences",
    "gtk-preferences"
};

static void sync_panel(void)
{
    uint32_t at;

    for (at = 0U; at < OPENGAT_SHELL_MAX_WINDOWS &&
            at < OPENGAT_PANEL_MAX_TASKS; ++at) {
        struct opengat_panel_task task;
        uint32_t byte = 0U;

        if (!used[at]) {
            (void)opengat_panel_clear_task(at);
            continue;
        }
        /* The bar shows THIS desktop's windows, which is what
         * ShowAllDesks=0 in the panel's own profile asks for. */
        task.icon = APP_ICONS[apps[at]];
        task.active = opengat_shell_focused() == at &&
            !windows[at].minimised;
        task.minimised = windows[at].minimised;
        task.desktop = windows[at].desktop;
        while (windows[at].title[byte] != '\0' &&
                byte + 1U < OPENGAT_PANEL_LABEL_BYTES) {
            task.label[byte] = windows[at].title[byte];
            ++byte;
        }
        task.label[byte] = '\0';
        (void)opengat_panel_set_task(at, &task);
    }
}

void opengat_shell_draw(void)
{
    uint32_t at;

    if (!opengat_surface_valid(canvas)) {
        return;
    }
    sync_panel();
    /* FORWARDS: bottom first, so the top window is drawn last and covers
     * what it is over.  The same order the hit test walks backwards. */
    for (at = 0U; at < stack_depth; ++at) {
        uint32_t slot = stack[at];

        if (windows[slot].minimised ||
                windows[slot].desktop != shell_desktop) {
            continue;
        }
        opengat_window_draw(canvas, &windows[slot]);
        switch (apps[slot]) {
        case OPENGAT_APP_FILES:
            opengat_files_draw(canvas, &windows[slot]);
            break;
        case OPENGAT_APP_TERMINAL:
            opengat_terminal_draw(canvas, &windows[slot]);
            break;
        case OPENGAT_APP_TASKMGR:
            opengat_taskmgr_draw(canvas, &windows[slot]);
            break;
        case OPENGAT_APP_SETTINGS:
            opengat_settings_draw(canvas, &windows[slot]);
            break;
        case OPENGAT_APP_PACKAGES:
            opengat_packages_draw(canvas, &windows[slot]);
            break;
        default:
            break;
        }
    }
}

/*
 * The overlays, drawn AFTER the bar rather than with the windows: they
 * belong on top of everything including the panel that opened them, and
 * drawing them with the stack would put a window over an open menu.
 */
void opengat_shell_draw_overlays(void)
{
    struct opengat_rect button;

    if (!opengat_surface_valid(canvas)) {
        return;
    }
    if (menu_open && opengat_panel_plugin_bounds(shell_screen,
            OPENGAT_PANEL_PLUGIN_MENU, &button) == OPENGAT_PANEL_STATUS_OK) {
        opengat_menu_draw(canvas, shell_screen, button);
    }
    if (volume_open) {
        struct opengat_rect box = shell_volume_bounds();
        uint32_t lit;
        uint32_t at;

        if (box.height == 0U) {
            return;
        }
        opengat_surface_fill(canvas, box, box, OPENGAT_BG);
        for (at = 0U; at < box.width; ++at) {
            opengat_surface_plot(canvas, box, box.x + at, box.y,
                               OPENGAT_LINE);
            opengat_surface_plot(canvas, box, box.x + at,
                               box.y + box.height - 1U, OPENGAT_LINE);
        }
        for (at = 0U; at < box.height; ++at) {
            opengat_surface_plot(canvas, box, box.x, box.y + at,
                               OPENGAT_LINE);
            opengat_surface_plot(canvas, box, box.x + box.width - 1U,
                               box.y + at, OPENGAT_LINE);
        }
        /* The trough, and the level filled from the BOTTOM: a slider
         * that fills downwards reads as the amount you have lost. */
        {
            struct opengat_rect trough;

            trough.x = box.x + box.width / 2U - 2U;
            trough.y = box.y + 8U;
            trough.width = 4U;
            trough.height = box.height > 16U ? box.height - 16U : 0U;
            opengat_surface_fill(canvas, box, trough, OPENGAT_BASE);
            lit = trough.height * opengat_shell_volume() / 100U;
            {
                struct opengat_rect fill;

                fill.x = trough.x;
                fill.width = trough.width;
                fill.height = lit;
                fill.y = trough.y + trough.height - lit;
                opengat_surface_fill(canvas, box, fill, OPENGAT_SEL_BG);
            }
        }
    }
    if (run_open) {
        struct opengat_rect box;
        struct opengat_rect field;
        uint32_t at;

        box.width = 300U;
        box.height = run_error[0] != '\0' ? 96U : 78U;
        box.x = shell_screen.x + (shell_screen.width - box.width) / 2U;
        box.y = shell_screen.y + shell_screen.height / 3U;
        opengat_surface_fill(canvas, box, box, OPENGAT_BG);
        for (at = 0U; at < box.width; ++at) {
            opengat_surface_plot(canvas, box, box.x + at, box.y,
                               OPENGAT_LINE);
            opengat_surface_plot(canvas, box, box.x + at,
                               box.y + box.height - 1U, OPENGAT_LINE);
        }
        for (at = 0U; at < box.height; ++at) {
            opengat_surface_plot(canvas, box, box.x, box.y + at,
                               OPENGAT_LINE);
            opengat_surface_plot(canvas, box, box.x + box.width - 1U,
                               box.y + at, OPENGAT_LINE);
        }
        opengat_font_draw(canvas, box, box.x + 12U, box.y + 22U,
                        "Run:", OPENGAT_FG);
        field.x = box.x + 12U;
        field.y = box.y + 30U;
        field.width = box.width - 24U;
        field.height = 22U;
        opengat_surface_fill(canvas, box, field, OPENGAT_BASE);
        for (at = 0U; at < field.width; ++at) {
            opengat_surface_plot(canvas, box, field.x + at, field.y,
                               OPENGAT_LINE);
        }
        for (at = 0U; at < field.height; ++at) {
            opengat_surface_plot(canvas, box, field.x, field.y + at,
                               OPENGAT_LINE);
        }
        opengat_font_draw(canvas, field, field.x + 5U, field.y + 15U,
                        run_text, OPENGAT_TEXT);
        {
            /* A caret after the text, so the box looks like it is
             * taking the keyboard - which it is. */
            uint32_t pen = field.x + 5U + opengat_font_width(run_text);
            struct opengat_rect caret = { pen, field.y + 4U, 1U, 14U };

            opengat_surface_fill(canvas, field, caret, OPENGAT_TEXT);
        }
        if (run_error[0] != '\0') {
            opengat_font_draw(canvas, box, box.x + 12U, box.y + 74U,
                            run_error, OPENGAT_TEXT);
        }
    }
    if (switcher_open) {
        struct opengat_rect box;
        uint32_t order[OPENGAT_SHELL_MAX_WINDOWS];
        uint32_t live = switcher_list(order, OPENGAT_SHELL_MAX_WINDOWS);
        uint32_t at;

        if (live == 0U) {
            return;
        }
        box.width = 180U;
        box.height = 12U + live * 20U;
        box.x = shell_screen.x + (shell_screen.width - box.width) / 2U;
        box.y = shell_screen.y + (shell_screen.height - box.height) / 2U;
        opengat_surface_fill(canvas, box, box, OPENGAT_BG);
        for (at = 0U; at < box.width; ++at) {
            opengat_surface_plot(canvas, box, box.x + at, box.y,
                               OPENGAT_LINE);
            opengat_surface_plot(canvas, box, box.x + at,
                               box.y + box.height - 1U, OPENGAT_LINE);
        }
        for (at = 0U; at < box.height; ++at) {
            opengat_surface_plot(canvas, box, box.x, box.y + at,
                               OPENGAT_LINE);
            opengat_surface_plot(canvas, box, box.x + box.width - 1U,
                               box.y + at, OPENGAT_LINE);
        }
        /* In the same order the keys walk, so the marker is on the
         * window Alt+Tab will actually commit to. */
        for (at = 0U; at < live; ++at) {
            struct opengat_rect row;

            row.x = box.x + 3U;
            row.y = box.y + 6U + at * 20U;
            row.width = box.width - 6U;
            row.height = 20U;
            if (at == switcher_at) {
                opengat_surface_fill(canvas, box, row, OPENGAT_SEL_BG);
            }
            opengat_font_draw(canvas, row, row.x + 6U, row.y + 14U,
                windows[order[at]].title,
                at == switcher_at ? OPENGAT_SEL_FG : OPENGAT_FG);
        }
    }
    /*
     * Notifications stack up from the bar's top edge, newest at the
     * bottom - nearest where the eye already is when something on the
     * bar caused it.
     */
    {
        uint32_t at;

        for (at = 0U; at < note_count; ++at) {
            struct opengat_rect box;
            uint32_t edge;

            box.width = 220U;
            box.height = 46U;
            box.x = shell_screen.x + shell_screen.width - box.width - 10U;
            box.y = shell_screen.y + shell_screen.height -
                OPENGAT_PANEL_HEIGHT - 8U -
                (note_count - at) * (box.height + 6U);
            opengat_surface_fill(canvas, box, box, OPENGAT_BG);
            for (edge = 0U; edge < box.width; ++edge) {
                opengat_surface_plot(canvas, box, box.x + edge, box.y,
                                   OPENGAT_LINE);
                opengat_surface_plot(canvas, box, box.x + edge,
                                   box.y + box.height - 1U, OPENGAT_LINE);
            }
            for (edge = 0U; edge < box.height; ++edge) {
                opengat_surface_plot(canvas, box, box.x, box.y + edge,
                                   OPENGAT_LINE);
                opengat_surface_plot(canvas, box, box.x + box.width - 1U,
                                   box.y + edge, OPENGAT_LINE);
            }
            opengat_font_draw(canvas, box, box.x + 10U, box.y + 18U,
                            notes[at].title, OPENGAT_FG);
            opengat_font_draw(canvas, box, box.x + 10U, box.y + 34U,
                            notes[at].body, OPENGAT_TEXT);
        }
    }
    /*
     * The tip last of all, because it is the thing nearest the pointer
     * and nothing should be able to cover it.  GTK's tooltip is a pale
     * yellow box, which is what tooltip_bg_color is in every GTK2 theme
     * that ships one - it is not the widget background, and using the
     * widget background is how a tip stops looking like a tip.
     */
    if (context_open) {
        struct opengat_rect box = opengat_shell_context_bounds();
        uint32_t at;

        opengat_surface_fill(canvas, box, box, OPENGAT_BG);
        for (at = 0U; at < box.width; ++at) {
            opengat_surface_plot(canvas, box, box.x + at, box.y,
                               OPENGAT_LINE);
            opengat_surface_plot(canvas, box, box.x + at,
                               box.y + box.height - 1U, OPENGAT_LINE);
        }
        for (at = 0U; at < box.height; ++at) {
            opengat_surface_plot(canvas, box, box.x, box.y + at,
                               OPENGAT_LINE);
            opengat_surface_plot(canvas, box, box.x + box.width - 1U,
                               box.y + at, OPENGAT_LINE);
        }
        for (at = 0U; at < CONTEXT_ROWS; ++at) {
            /* Properties is DIMMED rather than left out: the menu keeps
             * pcmanfm's shape and nothing in it pretends to work. */
            opengat_font_draw(canvas, box, box.x + 10U,
                box.y + 4U + at * CONTEXT_ROW_H + 14U,
                CONTEXT_LABELS[at],
                at == 3U ? OPENGAT_LINE : OPENGAT_FG);
        }
    }
    if (rename_open) {
        struct opengat_rect box;
        struct opengat_rect field;
        uint32_t at;

        box.width = 280U;
        box.height = rename_error[0] != '\0' ? 96U : 78U;
        box.x = shell_screen.x + (shell_screen.width - box.width) / 2U;
        box.y = shell_screen.y + shell_screen.height / 3U;
        opengat_surface_fill(canvas, box, box, OPENGAT_BG);
        for (at = 0U; at < box.width; ++at) {
            opengat_surface_plot(canvas, box, box.x + at, box.y,
                               OPENGAT_LINE);
            opengat_surface_plot(canvas, box, box.x + at,
                               box.y + box.height - 1U, OPENGAT_LINE);
        }
        for (at = 0U; at < box.height; ++at) {
            opengat_surface_plot(canvas, box, box.x, box.y + at,
                               OPENGAT_LINE);
            opengat_surface_plot(canvas, box, box.x + box.width - 1U,
                               box.y + at, OPENGAT_LINE);
        }
        opengat_font_draw(canvas, box, box.x + 12U, box.y + 22U,
                        "Rename to:", OPENGAT_FG);
        field.x = box.x + 12U;
        field.y = box.y + 30U;
        field.width = box.width - 24U;
        field.height = 22U;
        opengat_surface_fill(canvas, box, field, OPENGAT_BASE);
        for (at = 0U; at < field.width; ++at) {
            opengat_surface_plot(canvas, box, field.x + at, field.y,
                               OPENGAT_LINE);
        }
        for (at = 0U; at < field.height; ++at) {
            opengat_surface_plot(canvas, box, field.x, field.y + at,
                               OPENGAT_LINE);
        }
        opengat_font_draw(canvas, field, field.x + 5U, field.y + 15U,
                        rename_text, OPENGAT_TEXT);
        {
            uint32_t pen = field.x + 5U + opengat_font_width(rename_text);
            struct opengat_rect caret = { pen, field.y + 4U, 1U, 14U };

            opengat_surface_fill(canvas, field, caret, OPENGAT_TEXT);
        }
        if (rename_error[0] != '\0') {
            opengat_font_draw(canvas, box, box.x + 12U, box.y + 74U,
                            rename_error, OPENGAT_TEXT);
        }
    }
    if (opengat_shell_tip_visible()) {
        struct opengat_rect box = opengat_shell_tip_bounds();
        uint32_t edge;

        opengat_surface_fill(canvas, box, box, 0xF5F5B5U);
        for (edge = 0U; edge < box.width; ++edge) {
            opengat_surface_plot(canvas, box, box.x + edge, box.y,
                               0x000000U);
            opengat_surface_plot(canvas, box, box.x + edge,
                               box.y + box.height - 1U, 0x000000U);
        }
        for (edge = 0U; edge < box.height; ++edge) {
            opengat_surface_plot(canvas, box, box.x, box.y + edge,
                               0x000000U);
            opengat_surface_plot(canvas, box, box.x + box.width - 1U,
                               box.y + edge, 0x000000U);
        }
        opengat_font_draw(canvas, box, box.x + 7U, box.y + 14U,
                        tip_text, 0x000000U);
    }
}

uint32_t opengat_shell_run(opengat_event_source next, opengat_present_fn redraw,
    void *context)
{
    struct opengat_event event;
    uint32_t handled = 0U;

    if (next == NULL) {
        return 0U;
    }
    /* Paint once BEFORE the first event, or the desktop is not on screen
     * until somebody touches it. */
    if (redraw != NULL) {
        redraw(context);
    }
    while (next(&event, context)) {
        if (!opengat_shell_handle(&event)) {
            continue;
        }
        ++handled;
        if (redraw != NULL) {
            redraw(context);
        }
    }
    return handled;
}

/*
 * The self test asks the three things a window manager is FOR, and that
 * a pile of independently-correct modules still gets wrong:
 *
 *   - does a click land on the window you can SEE, not the one under it?
 *   - does clicking a window raise it, so the next click lands there?
 *   - when a window closes, does focus go somewhere real?
 */
bool opengat_shell_self_test(void)
{
    struct opengat_event press;
    uint32_t lower;
    uint32_t upper;

    opengat_shell_reset(canvas);
    opengat_shell_set_screen((struct opengat_rect){ 0U, 0U, 1280U, 800U });
    lower = opengat_shell_open(OPENGAT_APP_TASKMGR,
        (struct opengat_rect){ 100U, 100U, 300U, 200U });
    upper = opengat_shell_open(OPENGAT_APP_TERMINAL,
        (struct opengat_rect){ 200U, 150U, 300U, 200U });
    if (lower >= OPENGAT_SHELL_MAX_WINDOWS ||
            upper >= OPENGAT_SHELL_MAX_WINDOWS) {
        return false;
    }
    /* Opened second, so it is on top and focused. */
    if (opengat_shell_focused() != upper) {
        return false;
    }
    /* A point inside BOTH frames belongs to the upper one. */
    if (opengat_shell_at(250U, 200U) != upper) {
        return false;
    }
    /* A point inside only the lower one belongs to it. */
    if (opengat_shell_at(120U, 120U) != lower) {
        return false;
    }
    /* Clicking the lower one raises it, and then the overlap is ITS. */
    press.kind = OPENGAT_EVENT_POINTER_DOWN;
    press.x = 120U;
    press.y = 120U;
    press.modifiers = 0U;
    press.key = 0;
    press.special = 0U;
    press.double_click = false;
    if (!opengat_shell_handle(&press)) {
        return false;
    }
    if (opengat_shell_focused() != lower) {
        return false;
    }
    if (opengat_shell_at(250U, 200U) != lower) {
        return false;
    }
    /* Closing the focused one leaves focus on something real. */
    if (!opengat_shell_close(lower)) {
        return false;
    }
    if (opengat_shell_focused() != upper) {
        return false;
    }
    if (opengat_shell_window_count() != 1U) {
        return false;
    }
    /* Closing the last one leaves nothing focused, and says so rather
     * than returning a slot that is not open. */
    if (!opengat_shell_close(upper)) {
        return false;
    }
    if (opengat_shell_focused() != OPENGAT_SHELL_MAX_WINDOWS) {
        return false;
    }
    if (opengat_shell_window(upper) != NULL) {
        return false;
    }

    /*
     * And the bar.  A press on a launcher has to OPEN something, and a
     * press on the button of the focused window has to put it down -
     * both were pictures until the panel got a hit test.
     */
    {
        struct opengat_panel_hit hit;
        uint32_t opened;

        opengat_shell_reset(canvas);
        /* The self-test may run before a surface exists, so it says what
         * the screen is rather than inferring it from one.  Without this
         * the work area is nought by nought and "maximised" means a
         * window of no size - which is what the first run of this found. */
        opengat_shell_set_screen((struct opengat_rect){ 0U, 0U, 1280U, 800U });
        (void)opengat_panel_initialize();
        hit.kind = OPENGAT_PANEL_HIT_LAUNCHER;
        hit.index = 0U;
        if (!shell_panel_press(hit)) {
            return false;
        }
        if (opengat_shell_window_count() != 1U) {
            return false;
        }
        opened = opengat_shell_focused();
        if (opengat_shell_app_of(opened) != OPENGAT_APP_FILES) {
            return false;
        }
        /* A launcher index the bar does not have opens nothing rather
         * than reading off the end of the table. */
        hit.index = 9U;
        if (shell_panel_press(hit)) {
            return false;
        }
        if (opengat_shell_window_count() != 1U) {
            return false;
        }
        /* The focused window's own task button minimises it. */
        hit.kind = OPENGAT_PANEL_HIT_TASK;
        hit.index = opened;
        if (!shell_panel_press(hit)) {
            return false;
        }
        if (!windows[opened].minimised) {
            return false;
        }
        /* A minimised window is not under the pointer any more. */
        if (opengat_shell_at(windows[opened].frame.x + 5U,
                windows[opened].frame.y + 5U) <
                OPENGAT_SHELL_MAX_WINDOWS) {
            return false;
        }
        /* And pressing it again brings it back. */
        if (!shell_panel_press(hit)) {
            return false;
        }
        if (windows[opened].minimised) {
            return false;
        }
        /* A pager press changes the shell's visible workspace and focus,
         * not only the panel's highlighted cell. */
        hit.kind = OPENGAT_PANEL_HIT_PAGER;
        hit.index = 1U;
        if (!shell_panel_press(hit) || opengat_shell_desktop() != 1U ||
                opengat_shell_focused() != OPENGAT_SHELL_MAX_WINDOWS) {
            return false;
        }
        hit.index = 2U;
        if (shell_panel_press(hit)) {
            return false;
        }
        opengat_shell_set_desktop(0U);
        if (opengat_shell_focused() != opened) {
            return false;
        }
        /* Maximise fills the work area and stops at the panel. */
        toggle_maximise(opened, shell_screen);
        if (!windows[opened].maximised) {
            return false;
        }
        if (windows[opened].frame.y + windows[opened].frame.height +
                OPENGAT_PANEL_HEIGHT != shell_screen.height) {
            return false;
        }
        /* And unmaximising puts it back where it was, not somewhere
         * plausible. */
        {
            struct opengat_rect was = windows[opened].restore;

            toggle_maximise(opened, shell_screen);
            if (windows[opened].frame.x != was.x ||
                    windows[opened].frame.y != was.y ||
                    windows[opened].frame.width != was.width ||
                    windows[opened].frame.height != was.height) {
                return false;
            }
        }
    }
    opengat_shell_reset(canvas);
    return true;
}
