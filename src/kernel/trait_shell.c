/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/shell.h>

#include <trait/files.h>
#include <trait/font.h>
#include <trait/gears.h>
#include <trait/packages.h>
#include <trait/menu.h>
#include <trait/theme.h>
#include <trait/window.h>
#include <trait/settings.h>
#include <trait/taskmgr.h>
#include <trait/terminal.h>

static struct trait_surface *canvas;
static struct trait_rect shell_screen;
static uint32_t shell_desktop;

static bool context_open;
static uint32_t context_x;
static uint32_t context_y;
static uint32_t context_node = TRAIT_FILES_MAX_NODES;

static bool rename_open;
static char rename_text[TRAIT_FILES_NAME_BYTES];
static uint32_t rename_length;
static char rename_error[48];

#define CONTEXT_ROWS 4U
#define CONTEXT_ROW_H 20U
#define CONTEXT_W 150U

/*
 * THE LAUNCHER IS DMENU, and the geometry below is dmenu's own.
 *
 * It was a box in the middle of the screen: you typed a name, pressed
 * return, and it either ran or told you there was no such program.  It
 * could not tell you what there WAS, so the only way to use it was to
 * already know.
 *
 * dmenu answers that by showing you: a strip across the top, the
 * matches laid out along it, and what you type narrowing them.  From
 * dmenu.c - the bar is the font's height plus two, the padding either
 * side of a cell is half the font's height, the prompt is drawn in the
 * SELECTED colours and the input in the normal ones, and a '>' says
 * there are more matches than fit.  From dmenu.1 - Tab copies the
 * selected match into the input, Return confirms, Escape leaves.
 */
static bool run_open;
static char run_text[48];
static uint32_t run_length;
static uint32_t run_at;          /* which match is selected */
static char run_error[64];

#define RUN_PROMPT "run:"

static const struct {
    const char *name;
    enum trait_shell_app app;
} RUNNABLE[3] = {
    { "lxterminal", TRAIT_APP_TERMINAL },
    { "Files", TRAIT_APP_FILES },
    { "glxgears", TRAIT_APP_GEARS }
};

#define RUNNABLE_COUNT (sizeof(RUNNABLE) / sizeof(RUNNABLE[0]))

static struct {
    char title[TRAIT_SHELL_NOTE_BYTES];
    char body[TRAIT_SHELL_NOTE_BYTES];
    uint32_t life;
} notes[TRAIT_SHELL_MAX_NOTES];
static uint32_t note_count;


/* Resizing: which edge is being dragged, and the frame it started from. */
static bool resizing;
static uint32_t resize_slot;
static uint32_t resize_edges;
static struct trait_rect resize_from;
static uint32_t resize_ox;
static uint32_t resize_oy;

#define EDGE_LEFT 0x1U
#define EDGE_RIGHT 0x2U
#define EDGE_TOP 0x4U
#define EDGE_BOTTOM 0x8U
#define RESIZE_GRIP TRAIT_BORDER
#define MIN_WINDOW 180U

static bool switcher_open;
static uint32_t switcher_at;

/* The desktop folder, and the two standard marks before it. */
/* The home folder, and that is all.  There was a Trash beside it and
 * nothing in this desktop moves a file to a trash or takes one back
 * out, so it was a picture of a control. */
#define DESKTOP_STANDARD 1U
#define DESKTOP_CELL_W 86U
#define DESKTOP_CELL_H 40U
/* The icons are 16 and there is no larger one of them; see
 * assets/icons/gentoo/SOURCE.txt. */
#define DESKTOP_MARK 16U
#define DESKTOP_MARGIN 8U
static uint32_t desktop_folder = TRAIT_FILES_MAX_NODES;
/*
 * OFF.  A fresh X session comes up on a bare root, and this is what a
 * bare root is: the gears and nothing over them.  The switch is in
 * Settings for anyone who wants ~/Desktop drawn on it.
 */
static bool desktop_icons;
static bool root_menu;              /* the menu the root press opened */
static struct trait_rect root_anchor;
static struct trait_window windows[TRAIT_SHELL_MAX_WINDOWS];
static enum trait_shell_app apps[TRAIT_SHELL_MAX_WINDOWS];
static bool used[TRAIT_SHELL_MAX_WINDOWS];

/*
 * THE STACK, TOP LAST.  Drawing walks it forwards and hit-testing walks
 * it backwards, which is the whole of why a click lands on the window you
 * can see rather than the one underneath it.  Holding the order in one
 * array rather than as a z field per window means the two can never
 * disagree.
 */
static uint32_t stack[TRAIT_SHELL_MAX_WINDOWS];
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

static const char *const TITLES[TRAIT_APP_COUNT] = {
    "WVRM Files", "user@openrfs: ~", "Task Manager", "Desktop Preferences",
    "Package Manager", "glxgears"
};

static void set_title(struct trait_window *window, const char *text)
{
    trait_window_set_title(window, text);
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
    if (stack_depth < TRAIT_SHELL_MAX_WINDOWS) {
        stack[stack_depth++] = slot;
    }
}

void trait_shell_reset(struct trait_surface *surface)
{
    uint32_t at;

    canvas = surface;
    shell_screen.x = 0U;
    shell_screen.y = 0U;
    shell_screen.width = surface != NULL ? surface->width : 0U;
    shell_screen.height = surface != NULL ? surface->height : 0U;
    shell_desktop = 0U;
    /* A reset restores the DEFAULTS, not just the volatile state.  Left
     * out, a settings press in one check leaked into the next and the
     * desktop came up with no icons in a test that had never touched
     * them. */
    desktop_icons = false;
    root_menu = false;
    /* The theme too. It is static state like the rest, and a check that
     * switched it left every screenshot after it in the wrong palette -
     * which is the same leak the comment above is about, one line up. */
    (void)trait_theme_select(TRAIT_THEME_DEFAULT);
    run_open = false;
    run_length = 0U;
    run_at = 0U;
    run_text[0] = '\0';
    run_error[0] = '\0';
    switcher_open = false;
    note_count = 0U;
    resizing = false;
    context_open = false;
    rename_open = false;
    rename_length = 0U;
    rename_text[0] = '\0';
    rename_error[0] = '\0';
    stack_depth = 0U;
    dragging = false;
    dragging_entry = false;
    for (at = 0U; at < TRAIT_SHELL_MAX_WINDOWS; ++at) {
        used[at] = false;
    }
}

void trait_shell_set_screen(struct trait_rect screen)
{
    shell_screen = screen;
}

void trait_shell_set_desktop(uint32_t desktop)
{
    uint32_t at;

    shell_desktop = desktop;
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
            trait_shell_focus(slot);
            return;
        }
    }
    for (at = 0U; at < TRAIT_SHELL_MAX_WINDOWS; ++at) {
        windows[at].active = false;
    }
}

uint32_t trait_shell_desktop(void)
{
    return shell_desktop;
}

void trait_shell_send_to_desktop(uint32_t slot, uint32_t desktop)
{
    if (slot >= TRAIT_SHELL_MAX_WINDOWS || !used[slot]) {
        return;
    }
    windows[slot].desktop = desktop;
}

static void copy_note(char *out, const char *text)
{
    uint32_t at = 0U;

    while (text != NULL && text[at] != '\0' &&
            at + 1U < TRAIT_SHELL_NOTE_BYTES) {
        out[at] = text[at];
        ++at;
    }
    out[at] = '\0';
}

void trait_shell_notify(const char *title, const char *body)
{
    uint32_t at;

    /* Full means the OLDEST goes, not the newest refused: the thing that
     * just happened is the thing worth saying. */
    if (note_count == TRAIT_SHELL_MAX_NOTES) {
        for (at = 1U; at < TRAIT_SHELL_MAX_NOTES; ++at) {
            notes[at - 1U] = notes[at];
        }
        --note_count;
    }
    copy_note(notes[note_count].title, title);
    copy_note(notes[note_count].body, body);
    notes[note_count].life = 12U;
    ++note_count;
}

uint32_t trait_shell_note_count(void)
{
    return note_count;
}

const char *trait_shell_note_title(uint32_t at)
{
    return at < note_count ? notes[at].title : "";
}

const char *trait_shell_note_body(uint32_t at)
{
    return at < note_count ? notes[at].body : "";
}

void trait_shell_tick(void)
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
}

static const char *const CONTEXT_LABELS[CONTEXT_ROWS] = {
    "Open", "Rename", "Delete", "Properties"
};

bool trait_shell_context_open(void)
{
    return context_open;
}

uint32_t trait_shell_context_row_count(void)
{
    return trait_files_read_only() ? 1U : CONTEXT_ROWS;
}

const char *trait_shell_context_row(uint32_t at)
{
    return at < trait_shell_context_row_count() ?
        CONTEXT_LABELS[at] : "";
}

struct trait_rect trait_shell_context_bounds(void)
{
    struct trait_rect box;

    box.width = CONTEXT_W;
    box.height = trait_shell_context_row_count() * CONTEXT_ROW_H + 8U;
    box.x = context_x;
    box.y = context_y;
    /* Kept on the screen: a menu opened near the right edge would run
     * off it, and near the foot would hang below the display. */
    if (box.x + box.width > shell_screen.x + shell_screen.width) {
        box.x = shell_screen.x + shell_screen.width - box.width;
    }
    if (box.y + box.height > shell_screen.y + shell_screen.height) {
        box.y = shell_screen.y + shell_screen.height - box.height;
    }
    return box;
}

uint32_t trait_shell_context_node(void)
{
    return context_node;
}

bool trait_shell_rename_open(void)
{
    return rename_open;
}

const char *trait_shell_rename_text(void)
{
    return rename_text;
}

const char *trait_shell_rename_error(void)
{
    return rename_error;
}

bool trait_shell_run_open(void)
{
    return run_open;
}

const char *trait_shell_run_text(void)
{
    return run_text;
}

const char *trait_shell_run_error(void)
{
    return run_error;
}

bool trait_shell_switcher_open(void)
{
    return switcher_open;
}

uint32_t trait_shell_switcher_at(void)
{
    return switcher_at;
}

/* ------------------------------------------------------ the root window */

void trait_shell_set_desktop_folder(uint32_t folder)
{
    desktop_folder = folder;
}

/*
 * The root weave.
 *
 * Two colours, every other pixel, offset by one on odd rows. At a period
 * of two it reads as a single darker tone from a normal distance and as
 * a weave up close, which is the whole trick and the reason X shipped it
 * instead of an image.
 */
void trait_shell_draw_root(void)
{
    struct trait_rect whole = { 0U, 0U, 0U, 0U };

    if (!trait_surface_valid(canvas)) {
        return;
    }
    /* The canvas, not shell_screen: the root is the whole framebuffer,
     * and shell_screen is zero until somebody has called
     * trait_shell_set_screen - which painted nothing at all. */
    whole.width = canvas->width;
    whole.height = canvas->height;
    /*
     * THE ROOT WEAVE, and it is the X server's, not a wallpaper.
     *
     * Before any window manager runs, before anything is started, the
     * root window of an X display is already filled with a four-by-four
     * tile the server builds for itself - MakeRootTile() in the X
     * server's dix/window.c, out of
     *
     *     static unsigned char _back_msb[4] = { 0x11, 0x44, 0x22, 0x88 };
     *
     * drawn as a bitmap with the screen's white as foreground and its
     * black as background.  Four rows, eight bits each, one white pixel
     * every fourth: a quarter-density stipple that reads as a dark grey
     * with a grain in it.  It is the oldest thing on an X screen and it
     * is what OpenBSD comes up on, because nothing in a bare session
     * paints over it.
     *
     * This was a flat fill of one grey for a while, which is what it
     * LOOKS like from across a room.  A flat root is also a root that a
     * see-through window cannot prove anything against: the terminal
     * mixes with what is behind it, and what is behind it was one
     * colour everywhere.  The grain is the difference between a window
     * you can see through and a window that is merely darker.
     */
    {
        static const uint8_t WEAVE[4] = { 0x11U, 0x44U, 0x22U, 0x88U };
        uint32_t x;
        uint32_t y;

        for (y = 0U; y < whole.height; ++y) {
            uint8_t row = WEAVE[y & 3U];

            for (x = 0U; x < whole.width; ++x) {
                /* MSB first, which is how PutImage reads an XYBitmap on
                 * a most-significant-bit-first screen. */
                bool lit = ((row >> (7U - (x & 7U))) & 1U) != 0U;

                trait_surface_plot(canvas, whole, x, y,
                                   lit ? 0xFFFFFFU : 0x000000U);
            }
        }
    }
}

/*
 * A press on the root opens the menu there. With no panel there is
 * nowhere else for it to live, which is how fvwm and twm have always
 * done it.
 */
bool trait_shell_root_press(uint32_t x, uint32_t y)
{
    struct trait_rect probe = { x, 0U, 1U, 1U };
    uint32_t height;
    uint32_t at;

    if (trait_shell_at(x, y) < trait_shell_window_count()) {
        return false;      /* a window is under it: not the root */
    }
    /*
     * AN ICON FIRST.  An iconified window's icon is on the root, so a
     * press on one would otherwise open the menu over it and the window
     * would have no way back except Alt+Tab - which is the taskbar
     * problem again, wearing a different coat.
     */
    for (at = 0U; at < trait_shell_window_icon_count(); ++at) {
        struct trait_rect cell;
        uint32_t slot = trait_shell_window_icon_slot(at);

        if (slot >= TRAIT_SHELL_MAX_WINDOWS ||
                !trait_shell_window_icon_bounds(at, &cell) ||
                !trait_rect_contains(cell, x, y)) {
            continue;
        }
        windows[slot].minimised = false;
        trait_shell_focus(slot);
        root_menu = false;
        return true;
    }
    /*
     * trait_menu_bounds grows the menu UPWARDS from its anchor, because
     * the panel it was written for sits at the bottom of the screen. A
     * root menu drops from the pointer instead. One call gives the
     * menu's height, and an anchor that far below the press makes the
     * existing rule put the menu's top where the press was.
     */
    height = trait_menu_bounds(shell_screen, probe).height;
    root_anchor.x = x;
    root_anchor.y = y + height;
    /* Except at the bottom, where dropping it would put it off the
     * screen. There it hangs above the pointer, the way it would. */
    if (y + height > shell_screen.height) {
        root_anchor.y = shell_screen.height;
    }
    root_anchor.width = 1U;
    root_anchor.height = 1U;
    root_menu = true;
    return true;
}

bool trait_shell_root_menu_open(void)
{
    return root_menu;
}

bool trait_shell_root_menu_bounds(struct trait_rect *out)
{
    if (out == NULL || !root_menu) {
        return false;
    }
    *out = trait_menu_bounds(shell_screen, root_anchor);
    return true;
}

void trait_shell_set_desktop_icons(bool show)
{
    desktop_icons = show;
}

bool trait_shell_desktop_icons(void)
{
    return desktop_icons;
}

/*
 * ONE PLACE.  Drawing walks this count and so does hit-testing, so
 * returning zero here both clears the desktop and stops a press landing
 * on an icon that is no longer drawn.  Honouring the switch in the
 * drawing code alone would have left the icons invisible and still
 * clickable, which is the same bug as a control that does nothing, in
 * reverse.
 */
uint32_t trait_shell_desktop_icon_count(void)
{
    if (!desktop_icons) {
        return 0U;
    }
    if (desktop_folder >= TRAIT_FILES_MAX_NODES) {
        return DESKTOP_STANDARD;
    }
    return DESKTOP_STANDARD + trait_files_visible_count(desktop_folder);
}

bool trait_shell_desktop_icon_bounds(uint32_t at, struct trait_rect *out)
{
    uint32_t rows;

    if (out == NULL || at >= trait_shell_desktop_icon_count()) {
        return false;
    }
    rows = (shell_screen.height > DESKTOP_MARGIN) ?
        (shell_screen.height - DESKTOP_MARGIN) / DESKTOP_CELL_H : 1U;
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

/*
 * ICONIFIED WINDOWS, ON THE ROOT.
 *
 * This is where a window GOES when you put it down, and it is the
 * reason there is no taskbar to miss.  fvwm drops an icon on the root
 * and that is how OpenBSD comes up; a minimised window that lives only
 * in Alt+Tab is a window you have to remember you have.
 *
 * Along the foot, left to right, because that is fvwm's default icon
 * box and because the top-left corner is where ~/Desktop already is.
 */
static uint32_t icon_list(uint32_t *out, uint32_t room)
{
    uint32_t found = 0U;
    uint32_t at;

    for (at = 0U; at < TRAIT_SHELL_MAX_WINDOWS && found < room; ++at) {
        if (used[at] && windows[at].minimised &&
                windows[at].desktop == shell_desktop) {
            out[found] = at;
            ++found;
        }
    }
    return found;
}

uint32_t trait_shell_window_icon_count(void)
{
    uint32_t list[TRAIT_SHELL_MAX_WINDOWS];

    return icon_list(list, TRAIT_SHELL_MAX_WINDOWS);
}

uint32_t trait_shell_window_icon_slot(uint32_t at)
{
    uint32_t list[TRAIT_SHELL_MAX_WINDOWS];
    uint32_t count = icon_list(list, TRAIT_SHELL_MAX_WINDOWS);

    return at < count ? list[at] : TRAIT_SHELL_MAX_WINDOWS;
}

bool trait_shell_window_icon_bounds(uint32_t at, struct trait_rect *out)
{
    uint32_t columns;

    if (out == NULL || at >= trait_shell_window_icon_count()) {
        return false;
    }
    columns = shell_screen.width > DESKTOP_MARGIN ?
        (shell_screen.width - DESKTOP_MARGIN) / DESKTOP_CELL_W : 1U;
    if (columns == 0U) {
        columns = 1U;
    }
    out->width = DESKTOP_CELL_W;
    out->height = DESKTOP_CELL_H;
    out->x = shell_screen.x + DESKTOP_MARGIN +
        (at % columns) * DESKTOP_CELL_W;
    /* Counted up from the foot, so a second row of icons grows towards
     * the middle rather than off the bottom of the screen. */
    out->y = shell_screen.y + shell_screen.height - DESKTOP_MARGIN -
        (at / columns + 1U) * DESKTOP_CELL_H;
    return true;
}

static void draw_mark_and_label(struct trait_rect cell, const char *mark,
    const char *label)
{
    uint32_t width = trait_font_width(label);
    uint32_t pen = cell.x + (cell.width > width ?
        (cell.width - width) / 2U : 0U);
    uint32_t base = cell.y + DESKTOP_MARK + 16U;

    trait_files_draw_icon_at(canvas, cell, mark, DESKTOP_MARK,
        cell.x + (cell.width - DESKTOP_MARK) / 2U, cell.y + 4U);
    /*
     * pcmanfm's LXDE profile is desktop_fg=#ffffff with
     * desktop_shadow=#000000: white ink over a dark halo, which is what
     * keeps a label readable wherever it lands.  The halo is the same
     * text offset by one in each direction - cheaper than a blur and
     * what a one-pixel shadow IS.
     */
    trait_font_draw(canvas, cell, pen + 1U, base, label, 0x000000U);
    trait_font_draw(canvas, cell, pen, base + 1U, label, 0x000000U);
    trait_font_draw(canvas, cell, pen, base, label, 0xFFFFFFU);
}

void trait_shell_draw_window_icons(void)
{
    uint32_t at;

    if (!trait_surface_valid(canvas)) {
        return;
    }
    for (at = 0U; at < trait_shell_window_icon_count(); ++at) {
        uint32_t slot = trait_shell_window_icon_slot(at);
        struct trait_rect cell;

        if (slot >= TRAIT_SHELL_MAX_WINDOWS ||
                !trait_shell_window_icon_bounds(at, &cell)) {
            continue;
        }
        draw_mark_and_label(cell,
            apps[slot] == TRAIT_APP_FILES ? "folder" :
                "application-x-executable",
            windows[slot].title);
    }
}

void trait_shell_draw_desktop(void)
{
    static const char *const STANDARD[DESKTOP_STANDARD] = {
        "user-home"
    };
    static const char *const LABELS[DESKTOP_STANDARD] = {
        "user"
    };
    uint32_t at;

    if (!trait_surface_valid(canvas)) {
        return;
    }
    for (at = 0U; at < trait_shell_desktop_icon_count(); ++at) {
        struct trait_rect cell;
        const char *mark;
        const char *label;

        if (!trait_shell_desktop_icon_bounds(at, &cell)) {
            continue;
        }
        if (at < DESKTOP_STANDARD) {
            mark = STANDARD[at];
            label = LABELS[at];
        } else {
            uint32_t node = trait_files_child(desktop_folder,
                                              at - DESKTOP_STANDARD);

            if (node >= TRAIT_FILES_MAX_NODES) {
                continue;
            }
            mark = trait_files_node_mark(node);
            label = trait_files_node_name(node);
        }
        draw_mark_and_label(cell, mark, label);
    }
}

struct trait_rect trait_shell_screen(void)
{
    return shell_screen;
}

uint32_t trait_shell_open(enum trait_shell_app app, struct trait_rect at)
{
    uint32_t slot;

    if ((uint32_t)app >= TRAIT_APP_COUNT) {
        return TRAIT_SHELL_MAX_WINDOWS;
    }
    /* The installed session has one kernel-console viewport. Reopening its
     * terminal focuses that viewport rather than creating a second window
     * whose unselected contents would be only the imported UI model. */
    if (app == TRAIT_APP_TERMINAL) {
        for (slot = 0U; slot < TRAIT_SHELL_MAX_WINDOWS; ++slot) {
            if (used[slot] && apps[slot] == TRAIT_APP_TERMINAL) {
                windows[slot].minimised = false;
                trait_shell_focus(slot);
                return slot;
            }
        }
    }
    for (slot = 0U; slot < TRAIT_SHELL_MAX_WINDOWS; ++slot) {
        if (!used[slot]) {
            break;
        }
    }
    if (slot == TRAIT_SHELL_MAX_WINDOWS) {
        return TRAIT_SHELL_MAX_WINDOWS;
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
    trait_shell_focus(slot);
    return slot;
}

bool trait_shell_close(uint32_t slot)
{
    if (slot >= TRAIT_SHELL_MAX_WINDOWS || !used[slot]) {
        return false;
    }
    used[slot] = false;
    stack_remove(slot);
    /* Focus falls to whatever is now on top, not to nothing: a desktop
     * with windows open and none focused is a state nobody asked for. */
    if (stack_depth != 0U) {
        trait_shell_focus(stack[stack_depth - 1U]);
    }
    return true;
}

uint32_t trait_shell_window_count(void)
{
    return stack_depth;
}

struct trait_window *trait_shell_window(uint32_t slot)
{
    if (slot >= TRAIT_SHELL_MAX_WINDOWS || !used[slot]) {
        return NULL;
    }
    return &windows[slot];
}

enum trait_shell_app trait_shell_app_of(uint32_t slot)
{
    if (slot >= TRAIT_SHELL_MAX_WINDOWS || !used[slot]) {
        return TRAIT_APP_COUNT;
    }
    return apps[slot];
}

uint32_t trait_shell_at(uint32_t x, uint32_t y)
{
    uint32_t at = stack_depth;

    /* BACKWARDS: topmost first. */
    while (at != 0U) {
        uint32_t slot = stack[--at];

        if (!windows[slot].minimised &&
                windows[slot].desktop == shell_desktop &&
                trait_rect_contains(windows[slot].frame, x, y)) {
            return slot;
        }
    }
    return TRAIT_SHELL_MAX_WINDOWS;
}

uint32_t trait_shell_focused(void)
{
    if (stack_depth == 0U) {
        return TRAIT_SHELL_MAX_WINDOWS;
    }
    return stack[stack_depth - 1U];
}

void trait_shell_focus(uint32_t slot)
{
    uint32_t at;

    if (slot >= TRAIT_SHELL_MAX_WINDOWS || !used[slot]) {
        return;
    }
    stack_raise(slot);
    for (at = 0U; at < TRAIT_SHELL_MAX_WINDOWS; ++at) {
        windows[at].active = used[at] && at == slot;
    }
}

/* A press near a title-bar button counts as on it: the marks are eight
 * pixels and a pointer is not that accurate, so the box is grown by two
 * on every side.  The DRAWN mark is still the mark - this widens what
 * answers, not what is shown. */
static bool button_box(uint32_t slot, enum trait_window_button which,
    struct trait_rect *out)
{
    if (!trait_window_button_bounds(&windows[slot], which, out)) {
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
static void toggle_maximise(uint32_t slot, struct trait_rect screen)
{
    struct trait_window *window = &windows[slot];

    if (window->maximised) {
        window->frame = window->restore;
        window->maximised = false;
        return;
    }
    window->restore = window->frame;
    window->frame.x = screen.x;
    window->frame.y = screen.y;
    window->frame.width = screen.width;
    /* THE WHOLE SCREEN.  There is no bar to leave room for. */
    window->frame.height = screen.height;
    window->maximised = true;
}

static bool handle_client(uint32_t slot, const struct trait_event *event)
{
    struct trait_rect client = trait_window_client(&windows[slot]);
    uint32_t at;

    switch (apps[slot]) {
    case TRAIT_APP_TASKMGR: {
        struct trait_rect box;

        /* A press on a column header sorts by it. */
        for (at = 0U; at < TRAIT_TASKMGR_COLUMNS; ++at) {
            struct trait_rect head;

            if (!trait_taskmgr_header_bounds(&windows[slot],
                    (enum trait_taskmgr_column)at, &head)) {
                continue;
            }
            if (trait_rect_contains(head, event->x, event->y)) {
                trait_taskmgr_sort((enum trait_taskmgr_column)at);
                return true;
            }
        }
        /* End Task, before the rows: it sits over the list's own area
         * and a press on it must not also pick a row underneath. */
        if (trait_taskmgr_end_button(&windows[slot], &box) &&
                trait_rect_contains(box, event->x, event->y)) {
            uint32_t pid = trait_taskmgr_selected_pid();

            if (!trait_taskmgr_end_selected()) {
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
            if (pid >= TRAIT_SHELL_FIRST_PID &&
                    pid - TRAIT_SHELL_FIRST_PID <
                        TRAIT_SHELL_MAX_WINDOWS &&
                    used[pid - TRAIT_SHELL_FIRST_PID]) {
                (void)trait_shell_close(pid - TRAIT_SHELL_FIRST_PID);
            }
            trait_shell_notify("Task Manager", "Task ended");
            return true;
        }
        for (at = 0U; at < TRAIT_TASKMGR_MAX_ROWS; ++at) {
            struct trait_rect row;

            if (!trait_taskmgr_row_bounds(&windows[slot], at, &row)) {
                break;
            }
            if (trait_rect_contains(row, event->x, event->y)) {
                trait_taskmgr_select(at);
                return true;
            }
        }
        return false;
    }
    case TRAIT_APP_SETTINGS:
        for (at = 0U; at < trait_settings_page_count(); ++at) {
            struct trait_rect tab;

            if (!trait_settings_tab_bounds(&windows[slot], at, &tab)) {
                continue;
            }
            if (trait_rect_contains(tab, event->x, event->y)) {
                trait_settings_select(at);
                return true;
            }
        }
        /* And the rows on the page you are looking at. */
        for (at = 0U; at < TRAIT_SETTINGS_MAX_ROWS; ++at) {
            struct trait_rect row;

            if (!trait_settings_row_bounds(&windows[slot], at, &row)) {
                break;
            }
            if (trait_rect_contains(row, event->x, event->y)) {
                return trait_settings_press(trait_settings_selected(),
                                            at);
            }
        }
        return false;
    case TRAIT_APP_FILES:
        for (at = 0U;
                at < trait_files_visible_count(trait_files_here()); ++at) {
            struct trait_rect cell;
            uint32_t node;

            if (!trait_files_entry_bounds(&windows[slot], at, &cell)) {
                continue;
            }
            if (!trait_rect_contains(cell, event->x, event->y)) {
                continue;
            }
            node = trait_files_visible_child(trait_files_here(), at);
            if (event->secondary) {
                /* pcmanfm selects what you right-clicked before opening
                 * the menu, so the menu is unambiguously about it. */
                trait_files_select(node, false);
                context_open = true;
                context_node = node;
                context_x = event->x;
                context_y = event->y;
                return true;
            }
            dragging_entry = true;
            drag_node = node;
            if (event->double_click || trait_files_single_click()) {
                /* Opening a FILE is not opening a folder, and pretending
                 * it is would be the file manager lying about what it
                 * did.  Only a folder opens.
                 *
                 * Under single_click the FIRST press opens, which is the
                 * setting doing the one thing it is named for.  The
                 * select below is then unreachable for a folder, so a
                 * folder cannot be selected by clicking it - which is
                 * exactly what pcmanfm does in that mode too. */
                if (trait_files_open(node)) {
                    return true;
                }
                if (event->double_click) {
                    return true;
                }
            }
            trait_files_select(node,
                (event->modifiers & TRAIT_MOD_CTRL) != 0U);
            return true;
        }
        if (trait_rect_contains(client, event->x, event->y)) {
            trait_files_clear_selection();
            return true;
        }
        return false;
    case TRAIT_APP_PACKAGES: {
        struct trait_rect apply;

        if (trait_packages_apply_bounds(&windows[slot], &apply) &&
                trait_rect_contains(apply, event->x, event->y)) {
            uint32_t changed = trait_packages_apply();

            if (changed > 0U) {
                trait_shell_notify("Package Manager",
                                   "changes applied");
            }
            return true;
        }
        for (at = 0U; at < trait_packages_count(); ++at) {
            struct trait_rect row;

            row.x = client.x;
            row.y = client.y + 30U + at * 19U;
            row.width = client.width;
            row.height = 19U;
            if (trait_rect_contains(row, event->x, event->y)) {
                trait_packages_select(at);
                return true;
            }
        }
        return false;
    }
    case TRAIT_APP_TERMINAL:
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

/* Which row of the open menu a y coordinate is on.  Rules are shorter
 * than rows, so this walks them rather than dividing. */
static uint32_t shell_menu_row(struct trait_rect box, uint32_t y)
{
    uint32_t top = box.y + TRAIT_MENU_TITLE_HEIGHT + 4U;
    uint32_t at;

    for (at = 0U; at < trait_menu_row_count(); ++at) {
        uint32_t height = trait_menu_row_is_rule(at) ? 7U : 20U;

        if (y >= top && y < top + height) {
            return at;
        }
        top += height;
    }
    return trait_menu_row_count();
}

static bool label_is(const char *label, const char *name)
{
    uint32_t at = 0U;

    while (label[at] == name[at]) {
        if (label[at] == '\0') {
            return true;
        }
        ++at;
    }
    return false;
}

static bool shell_menu_pick(uint32_t row)
{
    struct trait_rect where = { 240U, 180U, 560U, 360U };
    const char *label = trait_menu_row_label(row);

    if (label == NULL) {
        return false;
    }
    /*
     * THE WHOLE LABEL, not its first letter.  This used to match on
     * label[0] because the panel's menu was built from packages and no
     * two categories began alike; a root menu has Settings next to
     * System Tools and Run next to Restart, and the first letter picks
     * the wrong one.
     */
    if (label_is(label, "xterm")) {
        return trait_shell_open(TRAIT_APP_TERMINAL, where) <
            TRAIT_SHELL_MAX_WINDOWS;
    }
    if (label_is(label, "Files")) {
        return trait_shell_open(TRAIT_APP_FILES, where) <
            TRAIT_SHELL_MAX_WINDOWS;
    }
    if (label_is(label, "Packages")) {
        return trait_shell_open(TRAIT_APP_PACKAGES, where) <
            TRAIT_SHELL_MAX_WINDOWS;
    }
    if (label_is(label, "Task Manager")) {
        return trait_shell_open(TRAIT_APP_TASKMGR, where) <
            TRAIT_SHELL_MAX_WINDOWS;
    }
    if (label_is(label, "Settings")) {
        return trait_shell_open(TRAIT_APP_SETTINGS, where) <
            TRAIT_SHELL_MAX_WINDOWS;
    }
    if (label_is(label, "Run...")) {
        run_open = true;
        run_length = 0U;
        run_at = 0U;
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
    struct trait_rect frame = windows[slot].frame;
    uint32_t edges = 0U;

    if (!trait_rect_contains(frame, x, y)) {
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
    struct trait_window *window = &windows[resize_slot];
    struct trait_rect frame = resize_from;
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
    if (context_node >= TRAIT_FILES_MAX_NODES ||
            row >= trait_shell_context_row_count()) {
        return false;
    }
    switch (row) {
    case 0U:     /* Open */
        return trait_files_open(context_node);
    case 1U: {   /* Rename */
        const char *name = trait_files_node_name(context_node);
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
        char body[TRAIT_SHELL_NOTE_BYTES];
        const char *name = trait_files_node_name(context_node);
        uint32_t at = 0U;

        while (name[at] != '\0' && at + 12U < sizeof(body)) {
            body[at] = name[at];
            ++at;
        }
        body[at] = '\0';
        if (!trait_files_remove(context_node)) {
            trait_shell_notify("Files", "That cannot be deleted");
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
        trait_shell_notify("Files", body);
        return true;
    }
    default:
        return false;
    }
}

static bool shell_rename_go(void)
{
    if (!trait_files_rename(context_node, rename_text)) {
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

/*
 * dmenu's match(), minus the buckets it does not need here: an item
 * matches when the input is somewhere in its name, and the ones that
 * match at the START come first.  Typing "lx" therefore offers
 * lxterminal before it offers anything that merely contains those two
 * letters, which is the ordering that makes the first match the one you
 * meant.
 */
static bool run_contains(const char *name, const char *want,
    uint32_t *at_start)
{
    uint32_t from = 0U;

    if (want[0] == '\0') {
        *at_start = 1U;
        return true;
    }
    while (name[from] != '\0') {
        uint32_t scan = 0U;

        while (want[scan] != '\0' && name[from + scan] == want[scan]) {
            ++scan;
        }
        if (want[scan] == '\0') {
            *at_start = from == 0U ? 1U : 0U;
            return true;
        }
        ++from;
    }
    return false;
}

/*
 * dmenu.c: `bh = drw->fonts->h + 2` and `mh = (lines + 1) * bh`, with
 * lines zero, so the strip is one row of the font plus two.  It sits at
 * the TOP unless -b is passed, and this is not passing -b.
 */
bool trait_shell_run_bounds(struct trait_rect *out)
{
    if (out == NULL || !run_open) {
        return false;
    }
    out->x = shell_screen.x;
    out->y = shell_screen.y;
    out->width = shell_screen.width;
    out->height = trait_font_line_height() + 2U;
    return true;
}

uint32_t trait_shell_run_match_count(void)
{
    uint32_t found = 0U;
    uint32_t at;
    uint32_t head;

    for (at = 0U; at < RUNNABLE_COUNT; ++at) {
        if (run_contains(RUNNABLE[at].name, run_text, &head)) {
            ++found;
        }
    }
    return found;
}

const char *trait_shell_run_match(uint32_t at)
{
    uint32_t pass;
    uint32_t seen = 0U;

    /* Two passes rather than a sort: the list is six long and a sort
     * would need somewhere to put it. */
    for (pass = 0U; pass < 2U; ++pass) {
        uint32_t scan;

        for (scan = 0U; scan < RUNNABLE_COUNT; ++scan) {
            uint32_t head = 0U;

            if (!run_contains(RUNNABLE[scan].name, run_text, &head)) {
                continue;
            }
            if ((pass == 0U) != (head == 1U)) {
                continue;
            }
            if (seen == at) {
                return RUNNABLE[scan].name;
            }
            ++seen;
        }
    }
    return NULL;
}

uint32_t trait_shell_run_selected(void)
{
    uint32_t count = trait_shell_run_match_count();

    return count == 0U ? 0U : (run_at < count ? run_at : count - 1U);
}

static void run_close(void)
{
    run_open = false;
    run_length = 0U;
    run_at = 0U;
    run_text[0] = '\0';
    run_error[0] = '\0';
}

static bool shell_run_go(void)
{
    struct trait_rect where = { 260U, 200U, 560U, 360U };
    const char *pick = trait_shell_run_match(trait_shell_run_selected());
    uint32_t at;
    uint32_t byte;

    if (pick != NULL) {
        for (at = 0U; at < RUNNABLE_COUNT; ++at) {
            if (RUNNABLE[at].name != pick) {
                continue;
            }
            run_close();
            return trait_shell_open(RUNNABLE[at].app, where) <
                TRAIT_SHELL_MAX_WINDOWS;
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

bool trait_shell_handle(const struct trait_event *event)
{
    uint32_t slot;
    struct trait_rect title;
    struct trait_rect close;

    if (event == NULL) {
        return false;
    }
    if (event->kind == TRAIT_EVENT_KEY) {
        /*
         * THE RUN BOX TAKES THE KEYBOARD while it is open, which is what
         * a modal dialog IS.  Without this, typing into it would also
         * reach the window behind it - the bug that makes a dialog feel
         * like a picture stuck to the screen.
         */
        if (rename_open) {
            if (event->special == TRAIT_KEY_ESCAPE) {
                rename_open = false;
                return true;
            }
            if (event->special == TRAIT_KEY_BACKSPACE) {
                if (rename_length != 0U) {
                    rename_text[--rename_length] = '\0';
                }
                return true;
            }
            if (event->special == TRAIT_KEY_ENTER) {
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
            uint32_t matches = trait_shell_run_match_count();

            if (event->special == TRAIT_KEY_ESCAPE) {
                run_close();
                return true;
            }
            if (event->special == TRAIT_KEY_BACKSPACE) {
                if (run_length != 0U) {
                    run_text[--run_length] = '\0';
                }
                /* Back to the first match: the list under the cursor
                 * just changed, so the place in it has not survived. */
                run_at = 0U;
                run_error[0] = '\0';
                return true;
            }
            if (event->special == TRAIT_KEY_ENTER) {
                return shell_run_go();
            }
            /* dmenu.1: the arrow keys select, and Tab copies the
             * selected item into the input. */
            if (event->special == TRAIT_KEY_RIGHT) {
                run_at = matches == 0U ? 0U :
                    (trait_shell_run_selected() + 1U) % matches;
                return true;
            }
            if (event->special == TRAIT_KEY_LEFT) {
                uint32_t here = trait_shell_run_selected();

                run_at = matches == 0U ? 0U :
                    (here == 0U ? matches - 1U : here - 1U);
                return true;
            }
            if (event->special == TRAIT_KEY_TAB) {
                const char *pick =
                    trait_shell_run_match(trait_shell_run_selected());

                if (pick == NULL) {
                    return false;
                }
                run_length = 0U;
                while (pick[run_length] != '\0' &&
                        run_length + 1U < sizeof(run_text)) {
                    run_text[run_length] = pick[run_length];
                    ++run_length;
                }
                run_text[run_length] = '\0';
                run_at = 0U;
                return true;
            }
            if (event->key >= 32 && event->key <= 126 &&
                    run_length + 1U < sizeof(run_text)) {
                run_text[run_length++] = event->key;
                run_text[run_length] = '\0';
                run_at = 0U;
                run_error[0] = '\0';
                return true;
            }
            return false;
        }
        /*
         * THE SHORTCUTS SETTINGS ADVERTISES, and they had to be written
         * before that page could keep claiming them.  Alt+F4 was real;
         * W-e, W-r and C-A-t were three lines of text on the Keyboard
         * page and nothing behind any of them.
         *
         * With no bar, the keyboard is half the ways in: the other is
         * the root menu, and neither of them is a button on a strip
         * that eats a row of the screen forever.
         */
        {
            struct trait_rect where = { 240U, 180U, 560U, 360U };

            if ((event->modifiers & TRAIT_MOD_SUPER) != 0U) {
                if (event->key == 'e') {
                    return trait_shell_open(TRAIT_APP_FILES, where) <
                        TRAIT_SHELL_MAX_WINDOWS;
                }
                if (event->key == 'r') {
                    run_open = true;
                    run_length = 0U;
                    run_at = 0U;
                    run_text[0] = '\0';
                    run_error[0] = '\0';
                    return true;
                }
            }
            if (event->key == 't' &&
                    (event->modifiers & TRAIT_MOD_CTRL) != 0U &&
                    (event->modifiers & TRAIT_MOD_ALT) != 0U) {
                return trait_shell_open(TRAIT_APP_TERMINAL, where) <
                    TRAIT_SHELL_MAX_WINDOWS;
            }
        }
        /*
         * ALT+TAB.  It is held open while Alt is down, so the state
         * lives here rather than being a one-shot: releasing Alt is what
         * commits the choice, which is how the real one works and why
         * tabbing twice goes two windows back rather than one.
         */
        if (event->special == TRAIT_KEY_TAB &&
                (event->modifiers & TRAIT_MOD_ALT) != 0U) {
            uint32_t order[TRAIT_SHELL_MAX_WINDOWS];
            uint32_t live = switcher_list(order, TRAIT_SHELL_MAX_WINDOWS);

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
                (event->modifiers & TRAIT_MOD_ALT) == 0U) {
            /* Alt came up: commit to whatever is under the marker. */
            uint32_t order[TRAIT_SHELL_MAX_WINDOWS];
            uint32_t live = switcher_list(order, TRAIT_SHELL_MAX_WINDOWS);

            switcher_open = false;
            if (switcher_at < live) {
                windows[order[switcher_at]].minimised = false;
                trait_shell_focus(order[switcher_at]);
            }
            return true;
        }
        slot = trait_shell_focused();

        if (slot >= TRAIT_SHELL_MAX_WINDOWS) {
            return false;
        }
        /* A-F4 closes the FOCUSED window, which is the one the keyboard
         * is talking to - not the one under the pointer. */
        if (event->special == TRAIT_KEY_F4 &&
                (event->modifiers & TRAIT_MOD_ALT) != 0U) {
            return trait_shell_close(slot);
        }
        /*
         * Ctrl+X/C/V reach the FOCUSED file manager.  They are handled
         * here rather than in files.c because the clipboard is a
         * desktop-wide thing: copy in one window, paste in another.
         */
        if (apps[slot] == TRAIT_APP_FILES &&
                (event->modifiers & TRAIT_MOD_CTRL) != 0U) {
            if (event->key == 'c' || event->key == 'x') {
                return trait_files_copy_selection(event->key == 'x');
            }
            if (event->key == 'v') {
                uint32_t moved =
                    trait_files_paste_into(trait_files_here());

                if (moved == 0U) {
                    return false;
                }
                trait_shell_notify("Files",
                    moved == 1U ? "1 item pasted" : "items pasted");
                return true;
            }
            if (event->key == 'a') {
                trait_files_select_all();
                return true;
            }
        }
        if (apps[slot] == TRAIT_APP_FILES && event->key == '\b') {
            return trait_files_up();
        }
        if (apps[slot] == TRAIT_APP_TERMINAL) {
            if (event->special == TRAIT_KEY_ENTER) {
                trait_terminal_enter();
                return true;
            }
            if (event->special == TRAIT_KEY_BACKSPACE) {
                trait_terminal_backspace();
                return true;
            }
            if (event->key != 0) {
                trait_terminal_type(event->key);
                return true;
            }
        }
        return false;
    }

    if (event->kind == TRAIT_EVENT_POINTER_MOVE) {
        if (resizing) {
            resize_to(event->x, event->y);
            return true;
        }
        if (!dragging) {
            return false;
        }
        windows[drag_slot].frame.x = event->x > drag_dx ?
            event->x - drag_dx : 0U;
        windows[drag_slot].frame.y = event->y > drag_dy ?
            event->y - drag_dy : 0U;
        return true;
    }

    if (event->kind == TRAIT_EVENT_POINTER_UP) {
        bool was = dragging || resizing;

        dragging = false;
        resizing = false;
        if (dragging_entry) {
            uint32_t over = trait_shell_at(event->x, event->y);

            dragging_entry = false;
            if (over < TRAIT_SHELL_MAX_WINDOWS &&
                    apps[over] == TRAIT_APP_FILES) {
                uint32_t at;

                for (at = 0U;
                        at < trait_files_visible_count(
                                 trait_files_here());
                        ++at) {
                    struct trait_rect cell;
                    uint32_t target;

                    if (!trait_files_entry_bounds(&windows[over], at,
                                                  &cell)) {
                        continue;
                    }
                    if (!trait_rect_contains(cell, event->x, event->y)) {
                        continue;
                    }
                    target = trait_files_child(trait_files_here(), at);
                    /* trait_files_move() refuses every bad case itself -
                     * onto a file, onto its own folder, into itself - so
                     * this does not have to know which they are. */
                    return trait_files_move(drag_node, target);
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
        struct trait_rect box = trait_shell_context_bounds();

        if (trait_rect_contains(box, event->x, event->y)) {
            uint32_t row = (event->y - box.y - 4U) / CONTEXT_ROW_H;

            context_open = false;
            return shell_context_pick(row);
        }
        context_open = false;
        /* fall through, so the press still lands where it landed */
    }
    if (root_menu) {
        struct trait_rect box = trait_menu_bounds(shell_screen,
                                                  root_anchor);

        if (trait_rect_contains(box, event->x, event->y)) {
            uint32_t row = shell_menu_row(box, event->y);

            root_menu = false;
            return shell_menu_pick(row);
        }
        root_menu = false;
        /* fall through: the press still lands where it landed */
    }

    slot = trait_shell_at(event->x, event->y);
    if (slot >= TRAIT_SHELL_MAX_WINDOWS) {
        return false;
    }
    /* Whatever else the press does, it RAISES: that is what clicking a
     * window means, and doing it before anything else means the rest of
     * this function is always talking about the window on top. */
    trait_shell_focus(slot);

    if (button_box(slot, TRAIT_WINDOW_CLOSE, &close) &&
            trait_rect_contains(close, event->x, event->y)) {
        return trait_shell_close(slot);
    }
    if (button_box(slot, TRAIT_WINDOW_MAXIMISE, &close) &&
            trait_rect_contains(close, event->x, event->y)) {
        toggle_maximise(slot, shell_screen);
        return true;
    }
    if (button_box(slot, TRAIT_WINDOW_MINIMISE, &close) &&
            trait_rect_contains(close, event->x, event->y)) {
        windows[slot].minimised = true;
        /* Focus goes to whatever is now the top VISIBLE window, not to
         * the one that just went away. */
        {
            uint32_t at = stack_depth;

            while (at != 0U) {
                uint32_t under = stack[--at];

                if (used[under] && !windows[under].minimised) {
                    trait_shell_focus(under);
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
    title = trait_window_title(&windows[slot]);
    if (trait_rect_contains(title, event->x, event->y)) {
        dragging = true;
        drag_slot = slot;
        drag_dx = event->x - windows[slot].frame.x;
        drag_dy = event->y - windows[slot].frame.y;
        return true;
    }
    (void)handle_client(slot, event);
    return true;
}

void trait_shell_draw(void)
{
    uint32_t at;

    if (!trait_surface_valid(canvas)) {
        return;
    }
    /* FORWARDS: bottom first, so the top window is drawn last and covers
     * what it is over.  The same order the hit test walks backwards. */
    for (at = 0U; at < stack_depth; ++at) {
        uint32_t slot = stack[at];

        if (windows[slot].minimised ||
                windows[slot].desktop != shell_desktop) {
            continue;
        }
        trait_window_draw(canvas, &windows[slot]);
        switch (apps[slot]) {
        case TRAIT_APP_FILES:
            trait_files_draw(canvas, &windows[slot]);
            break;
        case TRAIT_APP_TERMINAL:
            trait_terminal_draw(canvas, &windows[slot]);
            break;
        case TRAIT_APP_TASKMGR:
            trait_taskmgr_draw(canvas, &windows[slot]);
            break;
        case TRAIT_APP_SETTINGS:
            trait_settings_draw(canvas, &windows[slot]);
            break;
        case TRAIT_APP_GEARS:
            trait_gears_draw(canvas,
                             trait_window_client(&windows[slot]));
            break;
        case TRAIT_APP_PACKAGES:
            trait_packages_draw(canvas, &windows[slot]);
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
void trait_shell_draw_overlays(void)
{
    if (!trait_surface_valid(canvas)) {
        return;
    }
    if (root_menu) {
        trait_menu_draw(canvas, shell_screen, root_anchor);
    }
    if (run_open) {
        /*
         * dmenu's drawmenu(), with its own numbers:
         *   lrpad = drw->fonts->h        the padding, halved per side
         *   prompt in scheme[SchemeSel]  the selected colours
         *   input  in scheme[SchemeNorm] the normal ones
         *   each item drawn at x += its width, selected one in SchemeSel
         *   ">" at the right edge when the rest do not fit
         */
        struct trait_rect box;
        uint32_t pad = trait_font_line_height();
        uint32_t matches = trait_shell_run_match_count();
        uint32_t picked = trait_shell_run_selected();
        uint32_t baseline;
        uint32_t pen;
        uint32_t at;

        if (!trait_shell_run_bounds(&box)) {
            return;
        }
        baseline = box.y + box.height - 3U;
        trait_surface_fill(canvas, box, box, TRAIT_BG);

        {
            struct trait_rect head = box;

            head.width = trait_font_width(RUN_PROMPT) + pad;
            trait_surface_fill(canvas, box, head, TRAIT_SEL_BG);
            trait_font_draw(canvas, box, head.x + pad / 2U, baseline,
                            RUN_PROMPT, TRAIT_SEL_FG);
            pen = head.x + head.width;
        }
        trait_font_draw(canvas, box, pen + pad / 2U, baseline, run_text,
                        TRAIT_TEXT);
        {
            /* A caret after the input, because the strip has the
             * keyboard and ought to look like it. */
            struct trait_rect caret;

            caret.x = pen + pad / 2U + trait_font_width(run_text);
            caret.y = box.y + 2U;
            caret.width = 1U;
            caret.height = box.height - 4U;
            trait_surface_fill(canvas, box, caret, TRAIT_TEXT);
        }
        pen += trait_font_width(run_text) + pad * 2U;

        for (at = 0U; at < matches; ++at) {
            const char *name = trait_shell_run_match(at);
            uint32_t width;

            if (name == NULL) {
                break;
            }
            width = trait_font_width(name) + pad;
            if (pen + width > box.x + box.width - pad) {
                /* dmenu draws ">" when the rest will not fit, so you
                 * know the list did not end where the strip did. */
                trait_font_draw(canvas, box,
                    box.x + box.width - pad / 2U -
                        trait_font_width(">"),
                    baseline, ">", TRAIT_TEXT);
                break;
            }
            if (at == picked) {
                struct trait_rect cell;

                cell.x = pen;
                cell.y = box.y;
                cell.width = width;
                cell.height = box.height;
                trait_surface_fill(canvas, box, cell, TRAIT_SEL_BG);
                trait_font_draw(canvas, box, pen + pad / 2U, baseline,
                                name, TRAIT_SEL_FG);
            } else {
                trait_font_draw(canvas, box, pen + pad / 2U, baseline,
                                name, TRAIT_FG);
            }
            pen += width;
        }
        if (run_error[0] != '\0') {
            trait_font_draw(canvas, box,
                box.x + box.width - pad / 2U -
                    trait_font_width(run_error),
                baseline, run_error, TRAIT_TEXT);
        }
        /* A rule under it, so the strip has an edge against a window
         * that happens to be pale. */
        for (at = 0U; at < box.width; ++at) {
            trait_surface_plot(canvas, box, box.x + at,
                               box.y + box.height - 1U, TRAIT_LINE);
        }
    }
    if (switcher_open) {
        struct trait_rect box;
        uint32_t order[TRAIT_SHELL_MAX_WINDOWS];
        uint32_t live = switcher_list(order, TRAIT_SHELL_MAX_WINDOWS);
        uint32_t at;

        if (live == 0U) {
            return;
        }
        box.width = 180U;
        box.height = 12U + live * 20U;
        box.x = shell_screen.x + (shell_screen.width - box.width) / 2U;
        box.y = shell_screen.y + (shell_screen.height - box.height) / 2U;
        trait_surface_fill(canvas, box, box, TRAIT_BG);
        for (at = 0U; at < box.width; ++at) {
            trait_surface_plot(canvas, box, box.x + at, box.y,
                               TRAIT_LINE);
            trait_surface_plot(canvas, box, box.x + at,
                               box.y + box.height - 1U, TRAIT_LINE);
        }
        for (at = 0U; at < box.height; ++at) {
            trait_surface_plot(canvas, box, box.x, box.y + at,
                               TRAIT_LINE);
            trait_surface_plot(canvas, box, box.x + box.width - 1U,
                               box.y + at, TRAIT_LINE);
        }
        /* In the same order the keys walk, so the marker is on the
         * window Alt+Tab will actually commit to. */
        for (at = 0U; at < live; ++at) {
            struct trait_rect row;

            row.x = box.x + 3U;
            row.y = box.y + 6U + at * 20U;
            row.width = box.width - 6U;
            row.height = 20U;
            if (at == switcher_at) {
                trait_surface_fill(canvas, box, row, TRAIT_SEL_BG);
            }
            trait_font_draw(canvas, row, row.x + 6U, row.y + 14U,
                windows[order[at]].title,
                at == switcher_at ? TRAIT_SEL_FG : TRAIT_FG);
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
            struct trait_rect box;
            uint32_t edge;

            box.width = 220U;
            box.height = 46U;
            box.x = shell_screen.x + shell_screen.width - box.width - 10U;
            box.y = shell_screen.y + shell_screen.height - 8U -
                (note_count - at) * (box.height + 6U);
            trait_surface_fill(canvas, box, box, TRAIT_BG);
            for (edge = 0U; edge < box.width; ++edge) {
                trait_surface_plot(canvas, box, box.x + edge, box.y,
                                   TRAIT_LINE);
                trait_surface_plot(canvas, box, box.x + edge,
                                   box.y + box.height - 1U, TRAIT_LINE);
            }
            for (edge = 0U; edge < box.height; ++edge) {
                trait_surface_plot(canvas, box, box.x, box.y + edge,
                                   TRAIT_LINE);
                trait_surface_plot(canvas, box, box.x + box.width - 1U,
                                   box.y + edge, TRAIT_LINE);
            }
            trait_font_draw(canvas, box, box.x + 10U, box.y + 18U,
                            notes[at].title, TRAIT_FG);
            trait_font_draw(canvas, box, box.x + 10U, box.y + 34U,
                            notes[at].body, TRAIT_TEXT);
        }
    }
    if (context_open) {
        struct trait_rect box = trait_shell_context_bounds();
        uint32_t at;

        trait_surface_fill(canvas, box, box, TRAIT_BG);
        for (at = 0U; at < box.width; ++at) {
            trait_surface_plot(canvas, box, box.x + at, box.y,
                               TRAIT_LINE);
            trait_surface_plot(canvas, box, box.x + at,
                               box.y + box.height - 1U, TRAIT_LINE);
        }
        for (at = 0U; at < box.height; ++at) {
            trait_surface_plot(canvas, box, box.x, box.y + at,
                               TRAIT_LINE);
            trait_surface_plot(canvas, box, box.x + box.width - 1U,
                               box.y + at, TRAIT_LINE);
        }
        for (at = 0U; at < trait_shell_context_row_count(); ++at) {
            /* Properties is DIMMED rather than left out: the menu keeps
             * pcmanfm's shape and nothing in it pretends to work. */
            trait_font_draw(canvas, box, box.x + 10U,
                box.y + 4U + at * CONTEXT_ROW_H + 14U,
                CONTEXT_LABELS[at],
                at == 3U ? TRAIT_LINE : TRAIT_FG);
        }
    }
    if (rename_open) {
        struct trait_rect box;
        struct trait_rect field;
        uint32_t at;

        box.width = 280U;
        box.height = rename_error[0] != '\0' ? 96U : 78U;
        box.x = shell_screen.x + (shell_screen.width - box.width) / 2U;
        box.y = shell_screen.y + shell_screen.height / 3U;
        trait_surface_fill(canvas, box, box, TRAIT_BG);
        for (at = 0U; at < box.width; ++at) {
            trait_surface_plot(canvas, box, box.x + at, box.y,
                               TRAIT_LINE);
            trait_surface_plot(canvas, box, box.x + at,
                               box.y + box.height - 1U, TRAIT_LINE);
        }
        for (at = 0U; at < box.height; ++at) {
            trait_surface_plot(canvas, box, box.x, box.y + at,
                               TRAIT_LINE);
            trait_surface_plot(canvas, box, box.x + box.width - 1U,
                               box.y + at, TRAIT_LINE);
        }
        trait_font_draw(canvas, box, box.x + 12U, box.y + 22U,
                        "Rename to:", TRAIT_FG);
        field.x = box.x + 12U;
        field.y = box.y + 30U;
        field.width = box.width - 24U;
        field.height = 22U;
        trait_surface_fill(canvas, box, field, TRAIT_BASE);
        for (at = 0U; at < field.width; ++at) {
            trait_surface_plot(canvas, box, field.x + at, field.y,
                               TRAIT_LINE);
        }
        for (at = 0U; at < field.height; ++at) {
            trait_surface_plot(canvas, box, field.x, field.y + at,
                               TRAIT_LINE);
        }
        trait_font_draw(canvas, field, field.x + 5U, field.y + 15U,
                        rename_text, TRAIT_TEXT);
        {
            uint32_t pen = field.x + 5U + trait_font_width(rename_text);
            struct trait_rect caret = { pen, field.y + 4U, 1U, 14U };

            trait_surface_fill(canvas, field, caret, TRAIT_TEXT);
        }
        if (rename_error[0] != '\0') {
            trait_font_draw(canvas, box, box.x + 12U, box.y + 74U,
                            rename_error, TRAIT_TEXT);
        }
    }
}

uint32_t trait_shell_run(trait_event_source next, trait_present_fn redraw,
    void *context)
{
    struct trait_event event;
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
        if (!trait_shell_handle(&event)) {
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
bool trait_shell_self_test(void)
{
    struct trait_event press;
    uint32_t lower;
    uint32_t upper;

    trait_shell_reset(canvas);
    trait_shell_set_screen((struct trait_rect){ 0U, 0U, 1280U, 800U });
    lower = trait_shell_open(TRAIT_APP_TASKMGR,
        (struct trait_rect){ 100U, 100U, 300U, 200U });
    upper = trait_shell_open(TRAIT_APP_TERMINAL,
        (struct trait_rect){ 200U, 150U, 300U, 200U });
    if (lower >= TRAIT_SHELL_MAX_WINDOWS ||
            upper >= TRAIT_SHELL_MAX_WINDOWS) {
        return false;
    }
    /* Opened second, so it is on top and focused. */
    if (trait_shell_focused() != upper) {
        return false;
    }
    /* A point inside BOTH frames belongs to the upper one. */
    if (trait_shell_at(250U, 200U) != upper) {
        return false;
    }
    /* A point inside only the lower one belongs to it. */
    if (trait_shell_at(120U, 120U) != lower) {
        return false;
    }
    /* Clicking the lower one raises it, and then the overlap is ITS. */
    press.kind = TRAIT_EVENT_POINTER_DOWN;
    press.x = 120U;
    press.y = 120U;
    press.modifiers = 0U;
    press.key = 0;
    press.special = 0U;
    press.double_click = false;
    if (!trait_shell_handle(&press)) {
        return false;
    }
    if (trait_shell_focused() != lower) {
        return false;
    }
    if (trait_shell_at(250U, 200U) != lower) {
        return false;
    }
    /* Closing the focused one leaves focus on something real. */
    if (!trait_shell_close(lower)) {
        return false;
    }
    if (trait_shell_focused() != upper) {
        return false;
    }
    if (trait_shell_window_count() != 1U) {
        return false;
    }
    /* Closing the last one leaves nothing focused, and says so rather
     * than returning a slot that is not open. */
    if (!trait_shell_close(upper)) {
        return false;
    }
    if (trait_shell_focused() != TRAIT_SHELL_MAX_WINDOWS) {
        return false;
    }
    if (trait_shell_window(upper) != NULL) {
        return false;
    }

    /*
     * And the root menu, which is the only way in now that there is no
     * bar.  A press on the root has to OPEN it where the pointer is, a
     * press on a row has to start the thing the row names, and maximise
     * has to fill the WHOLE screen - there is nothing at the foot to
     * stop at any more.
     */
    {
        struct trait_rect box;
        uint32_t opened;

        trait_shell_reset(canvas);
        /* The self-test may run before a surface exists, so it says what
         * the screen is rather than inferring it from one.  Without this
         * the work area is nought by nought and "maximised" means a
         * window of no size - which is what the first run of this found. */
        trait_shell_set_screen((struct trait_rect){ 0U, 0U, 1280U, 800U });
        trait_menu_reset();
        if (!trait_menu_add("xterm", false, false) ||
                !trait_menu_add("Files", false, false)) {
            return false;
        }
        if (!trait_shell_root_press(400U, 300U)) {
            return false;
        }
        if (!trait_shell_root_menu_open() ||
                !trait_shell_root_menu_bounds(&box)) {
            return false;
        }
        /* The menu opens AT THE PRESS.  It used to grow upwards off a
         * button at the foot of the screen, and handed the press point
         * unchanged it opened above the pointer. */
        if (box.x < 400U || box.y < 300U) {
            return false;
        }
        {
            struct trait_event pick;
            uint8_t *zero = (uint8_t *)&pick;
            uint32_t byte;

            for (byte = 0U; byte < sizeof(pick); ++byte) {
                zero[byte] = 0U;
            }
            pick.kind = TRAIT_EVENT_POINTER_DOWN;
            pick.x = box.x + 20U;
            /* Past the title bar: the first ROW starts below it. */
            pick.y = box.y + TRAIT_MENU_TITLE_HEIGHT + 10U;
            if (!trait_shell_handle(&pick)) {
                return false;
            }
        }
        if (trait_shell_window_count() != 1U) {
            return false;
        }
        opened = trait_shell_focused();
        if (trait_shell_app_of(opened) != TRAIT_APP_TERMINAL) {
            return false;
        }
        /* And the menu is shut afterwards: a menu that stays open after
         * it has been used is a menu you have to dismiss twice. */
        if (trait_shell_root_menu_open()) {
            return false;
        }
        /* Maximise fills the screen, all of it. */
        toggle_maximise(opened, shell_screen);
        if (!windows[opened].maximised) {
            return false;
        }
        if (windows[opened].frame.y + windows[opened].frame.height !=
                shell_screen.height) {
            return false;
        }
        /* And unmaximising puts it back where it was, not somewhere
         * plausible. */
        {
            struct trait_rect was = windows[opened].restore;

            toggle_maximise(opened, shell_screen);
            if (windows[opened].frame.x != was.x ||
                    windows[opened].frame.y != was.y ||
                    windows[opened].frame.width != was.width ||
                    windows[opened].frame.height != was.height) {
                return false;
            }
        }
    }

    trait_shell_reset(canvas);
    return true;
}
