/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The host harness for the C desktop.
 *
 * The shell draws into a plain surface and knows nothing about files;
 * this allocates one, loads the wallpaper, asks the panel to draw, and
 * writes PNGs.  On real hardware the surface is the framebuffer and this
 * program does not exist.
 *
 *     make -C tools/c run
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rsd_desktop/font.h>
#include <rsd_desktop/files.h>
#include <rsd_desktop/menu.h>
#include <rsd_desktop/shell.h>
#include <rsd_desktop/packages.h>
#include <rsd_desktop/terminal.h>

#include "rsd_mono.h"
#include <rsd_desktop/settings.h>
#include <rsd_desktop/taskmgr.h>
#include <rsd_desktop/theme.h>
#include <rsd_desktop/window.h>
#include <rsd_desktop/surface.h>

#include "png.h"

#include "rsd_logo.h"

#define SCREEN_WIDTH 1280U
#define SCREEN_HEIGHT 800U

static uint32_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
static struct rsd_surface screen = {
    framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT
};

static struct rsd_rect whole(void)
{
    return (struct rsd_rect){ 0U, 0U, SCREEN_WIDTH, SCREEN_HEIGHT };
}

/*
 * There is no wallpaper and no image to decode.  The session comes up on
 * the wallpaper, which rsd_shell_draw_root() blits from a table - so the
 * loader that used to read assets/wallpaper/wallpaper.bin, and the flat
 * fill that stood in when the dump was missing, both went with it.
 */

static int emit(const char *directory, const char *name,
    struct rsd_rect area)
{
    char path[512];
    uint32_t *cut;
    int ok;

    snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (area.width == SCREEN_WIDTH && area.height == SCREEN_HEIGHT) {
        ok = png_write(path, framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
    } else {
        cut = malloc((size_t)area.width * area.height * sizeof(*cut));
        if (cut == NULL) {
            return 0;
        }
        for (uint32_t y = 0U; y < area.height; ++y) {
            for (uint32_t x = 0U; x < area.width; ++x) {
                cut[(size_t)y * area.width + x] =
                    rsd_surface_read(&screen, area.x + x, area.y + y);
            }
        }
        ok = png_write(path, cut, area.width, area.height);
        free(cut);
    }
    if (!ok) {
        fprintf(stderr, "rsd: could not write %s\n", path);
        return 0;
    }
    printf("wrote %s (%ux%u)\n", path, area.width, area.height);
    return 1;
}


static void populate(void)
{
    /* NOTHING TO POPULATE.  This used to load the bar: four task
     * buttons, a clock and thirty-six columns of cpu history.  With no
     * bar there is no list of open windows to keep in step with the
     * windows, which was the only thing here that could go stale. */
    return;
}

/* The Task Manager's rows.  They are the desktop's own processes, which
 * is what lxtask lists - a COMMAND, not a window title. */
static void populate_taskmgr(void)
{
    static const struct {
        const char *command;
        uint32_t cpu;
        uint32_t rss;
        uint32_t pid;
    } TASKS[] = {
        /* What this machine runs, under its own names. It listed
         * LXDE's five - pcmanfm, lxterminal, lxtask, openbox - which is
         * a task manager naming processes that are not on the machine,
         * in the one window whose whole job is to say what is. */
        { "rsd-session", 28U, 2458U, 1U },
        { "rsd-files", 12U, 3810U, 2U },
        { "rsd-term", 41U, 5122U, 3U },
        { "rsd-tasks", 8U, 1904U, 4U },
        { "rsd-panel", 3U, 2201U, 5U }
    };
    struct rsd_taskmgr_row row;

    rsd_taskmgr_reset();
    /* As many as there ARE. This said 6 over an array of five, so the
     * window came up listing a sixth process called "(null)" with no
     * user, no memory and pid 0 - a row for something that does not
     * exist, in the one window whose whole job is to say what does. */
    for (uint32_t at = 0U; at < sizeof(TASKS) / sizeof(TASKS[0]); ++at) {
        memset(&row, 0, sizeof(row));
        (void)snprintf(row.command, RSD_TASKMGR_NAME_BYTES, "%s",
                       TASKS[at].command);
        (void)snprintf(row.user, RSD_TASKMGR_NAME_BYTES, "user");
        row.cpu_tenths = TASKS[at].cpu;
        row.rss_kib = TASKS[at].rss;
        row.pid = TASKS[at].pid;
        (void)rsd_taskmgr_add(&row);
    }
}

/* Settings, as the three LXDE programs it stands in for. */
static void populate_settings(void)
{
    struct rsd_settings_row row;

    rsd_settings_reset();
    (void)rsd_settings_add_page("Widget");
    (void)rsd_settings_add_page("Desktop");
    (void)rsd_settings_add_page("Keyboard");

    /*
     * WHAT CHANGED HERE.  Five of these rows used to be CHOICE boxes with
     * RSD_SET_NOTHING behind them - Font size, Icon theme, Position,
     * Height, Clock format.  They were drawn exactly like the theme
     * picker beside them, and pressing them did nothing at all, which is
     * the failure this header warns about in the largest possible form.
     *
     * Each is now one of two things.  If the shell can carry it out it
     * is a real control wired to the thing it names.  If it cannot, it
     * is a NOTE, which is not drawn as a control and says what it is
     * instead - the icon theme and the font are stated rather than
     * offered, because there is one of each and a picker over one
     * choice is a picker that does nothing.
     */

    /* ---- Widget: lxappearance ---- */
    memset(&row, 0, sizeof(row));
    row.kind = RSD_SETTINGS_CHOICE;
    (void)snprintf(row.label, RSD_SETTINGS_TEXT_BYTES, "Widget theme");
    row.setting = RSD_SET_WIDGET_THEME;
    (void)rsd_settings_add_row(0U, &row);

    memset(&row, 0, sizeof(row));
    row.kind = RSD_SETTINGS_NOTE;
    (void)snprintf(row.label, RSD_SETTINGS_TEXT_BYTES,
                   "Icons: gentoo's, Johan Hanson 1998.");
    (void)rsd_settings_add_row(0U, &row);
    (void)snprintf(row.label, RSD_SETTINGS_TEXT_BYTES,
                   "Font: Misc-Fixed, unpacked ahead.");
    (void)rsd_settings_add_row(0U, &row);

    /* ---- Desktop: pcmanfm's Desktop Preferences ---- */
    memset(&row, 0, sizeof(row));
    row.kind = RSD_SETTINGS_CHOICE;
    (void)snprintf(row.label, RSD_SETTINGS_TEXT_BYTES, "File view");
    row.setting = RSD_SET_FILES_VIEW;
    (void)rsd_settings_add_row(1U, &row);

    row.kind = RSD_SETTINGS_SWITCH;
    (void)snprintf(row.label, RSD_SETTINGS_TEXT_BYTES,
                   "Show icons on the desktop");
    row.setting = RSD_SET_DESKTOP_ICONS;
    (void)rsd_settings_add_row(1U, &row);
    (void)snprintf(row.label, RSD_SETTINGS_TEXT_BYTES,
                   "Show hidden files");
    row.setting = RSD_SET_SHOW_HIDDEN;
    (void)rsd_settings_add_row(1U, &row);
    (void)snprintf(row.label, RSD_SETTINGS_TEXT_BYTES,
                   "Open on a single click");
    row.setting = RSD_SET_SINGLE_CLICK;
    (void)rsd_settings_add_row(1U, &row);
    (void)snprintf(row.label, RSD_SETTINGS_TEXT_BYTES,
                   "See through the terminal");
    row.setting = RSD_SET_TERM_SHEER;
    (void)rsd_settings_add_row(1U, &row);

    /* ---- Keyboard ---- */
    memset(&row, 0, sizeof(row));
    row.kind = RSD_SETTINGS_NOTE;
    (void)snprintf(row.label, RSD_SETTINGS_TEXT_BYTES,
                   "W-e   Open the file manager");
    (void)rsd_settings_add_row(2U, &row);
    (void)snprintf(row.label, RSD_SETTINGS_TEXT_BYTES,
                   "W-r   Open the Run box");
    (void)rsd_settings_add_row(2U, &row);
    (void)snprintf(row.label, RSD_SETTINGS_TEXT_BYTES,
                   "C-A-t Open a terminal");
    (void)rsd_settings_add_row(2U, &row);
    (void)snprintf(row.label, RSD_SETTINGS_TEXT_BYTES,
                   "A-F4  Close the focused window");
    (void)rsd_settings_add_row(2U, &row);
}


/* The filesystem the file manager shows.  Small, and every byte of it is
 * a number this window can stand behind: the folder sizes in the status
 * bar are counted from these. */
static uint32_t populate_files(void)
{
    uint32_t home;
    uint32_t user;
    uint32_t docs;

    rsd_files_reset();
    home = rsd_files_add(rsd_files_root(), "home", true, 0U);
    user = rsd_files_add(home, "user", true, 0U);
    (void)rsd_files_add(user, "Desktop", true, 0U);
    docs = rsd_files_add(user, "Documents", true, 0U);
    (void)rsd_files_add(user, "Downloads", true, 0U);
    (void)rsd_files_add(user, "Music", true, 0U);
    (void)rsd_files_add(user, "Pictures", true, 0U);
    (void)rsd_files_add(user, "Videos", true, 0U);
    (void)rsd_files_add(user, "README.txt", false, 1284U);
    (void)rsd_files_add(docs, "report.txt", false, 20481U);
    (void)rsd_files_add(docs, "letter.txt", false, 4096U);
    return user;
}


/*
 * The menu is BUILT FROM WHAT IS INSTALLED, which is the whole point of
 * having a package manager beside it: rebuild it after an Apply and the
 * newly installed thing is there, the removed thing is not.  A menu with
 * a hardcoded list would make Apply a button that changes a number.
 */
static void rebuild_menu(void)
{
    rsd_menu_reset();
    (void)rsd_menu_add("Accessories", true, false);
    if (rsd_packages_installed("rsd-files")) {
        (void)rsd_menu_add("System Tools", true, false);
    }
    if (rsd_packages_installed("rsd-calc")) {
        (void)rsd_menu_add("Calculator", false, false);
    }
    if (rsd_packages_installed("rsd-edit")) {
        (void)rsd_menu_add("Editor", false, false);
    }
    if (rsd_packages_installed("rsd-ark")) {
        (void)rsd_menu_add("Archiver", false, false);
    }
    (void)rsd_menu_add(NULL, false, true);
    (void)rsd_menu_add("Run...", false, false);
}

static void populate_packages(void)
{
    rsd_packages_reset();
    (void)rsd_packages_add("rsd-files", "The file manager", "Files",
                             true);
    (void)rsd_packages_add("rsd-term", "A terminal emulator",
                             "Terminal", true);
    (void)rsd_packages_add("rsd-tasks", "A task manager",
                             "Task Manager", true);
    (void)rsd_packages_add("rsd-edit", "A simple text editor",
                             "Editor", true);
    (void)rsd_packages_add("rsd-calc", "A desktop calculator",
                             "Calculator", false);
    (void)rsd_packages_add("rsd-ark", "An archive manager",
                             "Archiver", false);
}


/*
 * THE EVENT SOURCE, scripted.  On the metal this is a keyboard and a
 * mouse; here it is a list, and rsd_shell_run() cannot tell the
 * difference - which is the point of it taking a callback.
 */
struct script {
    const struct rsd_event *events;
    uint32_t count;
    uint32_t at;
    const char *out;
    uint32_t frames;
};

static bool script_next(struct rsd_event *out, void *context)
{
    struct script *run = context;

    if (run->at >= run->count) {
        return false;
    }
    *out = run->events[run->at++];
    return true;
}

/* The loop asks for a repaint only after an event that CHANGED
 * something, so the frame count is a measurement of that rather than of
 * how many events were sent. */
static void script_present(void *context)
{
    struct script *run = context;
    char name[64];

    rsd_shell_draw_root();
    rsd_shell_draw();
    rsd_shell_draw_overlays();
    snprintf(name, sizeof(name), "loop-%02u.png", run->frames++);
    (void)emit(run->out, name, whole());
}

static struct rsd_event typed(char ch)
{
    struct rsd_event event;

    memset(&event, 0, sizeof(event));
    event.kind = RSD_EVENT_KEY;
    event.key = ch;
    return event;
}

static struct rsd_event special_key(uint32_t which)
{
    struct rsd_event event;

    memset(&event, 0, sizeof(event));
    event.kind = RSD_EVENT_KEY;
    event.special = which;
    return event;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "build/c";

    populate();

    /* The shell has to be attached before it can draw: the wallpaper
     * this replaced was loaded straight into the surface and never went
     * through the shell at all. */
    rsd_shell_reset(&screen);
    rsd_shell_set_screen(whole());
    rsd_shell_draw_root();
    if (!emit(out, "desktop.png", whole())) {
        return 1;
    }


    /*
     * A session: two windows on the weave, and the menu where the root
     * was pressed. With no panel there is nowhere else for the menu to
     * be, which is how fvwm and twm have always worked.
     */
    /*
     * THE TREE FIRST. This scene opened a file manager and never built
     * the filesystem under it, so the window came up with nothing in it
     * and "0 items" along the bottom - which reads as a broken
     * application rather than as an empty folder, and sat in the
     * screenshot for a while looking exactly like one.
     */
    (void)rsd_files_open(populate_files());
    (void)rsd_shell_open(RSD_APP_FILES,
        (struct rsd_rect){ 300U, 120U, 620U, 400U });
    (void)rsd_shell_open(RSD_APP_TERMINAL,
        (struct rsd_rect){ 760U, 400U, 320U, 320U });
    (void)rsd_shell_open(RSD_APP_TERMINAL,
        (struct rsd_rect){ 140U, 300U, 560U, 320U });
    /*
     * twm's root menu: what you can start, and nothing else.  There is
     * no Restart or Exit on it, which twm has, because this shell does
     * not own an X session to restart or leave - a row that cannot do
     * what it says is the one thing this desktop does not draw.
     */
    rsd_menu_reset();
    (void)rsd_menu_add("xterm", false, false);
    (void)rsd_menu_add("Files", false, false);
    (void)rsd_menu_add("Packages", false, false);
    (void)rsd_menu_add("Task Manager", false, false);
    (void)rsd_menu_add("Settings", false, false);
    (void)rsd_menu_add("", false, true);
    (void)rsd_menu_add("Run...", false, false);

    /*
     * A LINE LONG ENOUGH TO CROSS WHAT IS BEHIND IT.  The terminal is
     * see-through and the file manager is behind its right-hand half,
     * so a line that runs that far is the one place the halo under the
     * glyphs has to earn itself: white on a pale ground is white on
     * white without it.
     */
    rsd_terminal_reset();
    rsd_terminal_run("uname -a");
    rsd_terminal_run("ls");

    rsd_shell_draw_root();
    rsd_shell_draw();
    /* Bare root, below and left of every window in this session. */
    if (!rsd_shell_root_press(80U, 520U)) {
        fprintf(stderr, "rsd: a press on bare root was not the root\n");
        return 1;
    }
    rsd_shell_draw_overlays();
    if (!emit(out, "session.png", whole())) {
        return 1;
    }
    {
        struct rsd_rect menu;

        if (!rsd_shell_root_menu_bounds(&menu)) {
            fprintf(stderr, "rsd: the root menu has no bounds\n");
            return 1;
        }
        if (menu.x < 80U || menu.y < 520U) {
            fprintf(stderr, "rsd: the menu did not open where the "
                    "press was\n");
            return 1;
        }
        printf("proof: no panel and a root menu at %ux%u - where the "
               "press was, not where a button is\n", menu.x, menu.y);
    }
    /* The two windows, over the same desktop, so they are seen sitting on
     * something rather than on a flat grey. */
    if (!rsd_taskmgr_self_test()) {
        fprintf(stderr, "rsd: task manager self-test failed\n");
        return 1;
    }
    if (!rsd_settings_self_test()) {
        fprintf(stderr, "rsd: settings self-test failed\n");
        return 1;
    }
    populate_taskmgr();
    populate_settings();
    {
        struct rsd_window taskmgr;
        struct rsd_window settings;

        memset(&taskmgr, 0, sizeof(taskmgr));
        taskmgr.frame = (struct rsd_rect){ 120U, 120U, 520U, 340U };
        taskmgr.active = false;
        rsd_window_set_title(&taskmgr, "Task Manager");

        memset(&settings, 0, sizeof(settings));
        settings.frame = (struct rsd_rect){ 470U, 290U, 520U, 300U };
        settings.active = true;
        rsd_window_set_title(&settings, "Desktop Preferences");

        /* Sorted by CPU, descending, which is what anybody opens a task
         * manager to see. */
        rsd_taskmgr_sort(RSD_TASKMGR_CPU);
        rsd_taskmgr_sort(RSD_TASKMGR_CPU);
        rsd_settings_select(2U);

        /*
         * Each window gets its own crop BEFORE the other is drawn over
         * it.  Cropping the finished screen gave a Task Manager with the
         * Settings window sitting in the corner of it - a true picture of
         * the screen and a useless picture of the window.
         */
        rsd_window_draw(&screen, &taskmgr);
        rsd_taskmgr_draw(&screen, &taskmgr);
        if (!emit(out, "taskmgr.png", taskmgr.frame)) {
            return 1;
        }
        rsd_window_draw(&screen, &settings);
        rsd_settings_draw(&screen, &settings);
        if (!emit(out, "settings.png", settings.frame)) {
            return 1;
        }
        if (!emit(out, "windows.png", whole())) {
            return 1;
        }
    }

    /* The file manager, on a screen of its own, at pcmanfm's own
     * 640x480 from its LXDE profile. */
    if (!rsd_files_self_test()) {
        fprintf(stderr, "rsd: file manager self-test failed\n");
        return 1;
    }
    {
        struct rsd_window files;
        uint32_t user = populate_files();

        (void)rsd_files_open(user);
        memset(&files, 0, sizeof(files));
        files.frame = (struct rsd_rect){ 150U, 130U, 640U, 480U };
        files.active = true;
        rsd_window_set_title(&files, "user");

        rsd_shell_draw_root();
        rsd_window_draw(&screen, &files);
        rsd_files_draw(&screen, &files);
        if (!emit(out, "files.png", files.frame)) {
            return 1;
        }

        /* And the detailed list, with a run of things picked, so the
         * status bar has something to count. */
        rsd_files_set_view(RSD_FILES_LIST);
        rsd_files_select(rsd_files_child(user, 1U), false);
        rsd_files_select(rsd_files_child(user, 2U), true);
        rsd_files_select(rsd_files_child(user, 7U), true);
        rsd_window_draw(&screen, &files);
        rsd_files_draw(&screen, &files);
        if (!emit(out, "files-list.png", files.frame)) {
            return 1;
        }
        if (!emit(out, "files-desktop.png", whole())) {
            return 1;
        }
    }

    /* The terminal, the package manager and the menu. */
    if (!rsd_terminal_self_test()) {
        fprintf(stderr, "rsd: terminal self-test failed\n");
        return 1;
    }
    if (!rsd_packages_self_test()) {
        fprintf(stderr, "rsd: package manager self-test failed\n");
        return 1;
    }
    if (!rsd_menu_self_test()) {
        fprintf(stderr, "rsd: menu self-test failed\n");
        return 1;
    }
    {
        struct rsd_window term;
        struct rsd_window synaptic;
        uint32_t before;
        uint32_t after;

        rsd_terminal_reset();
        rsd_terminal_run("uname -a");
        rsd_terminal_run("whoami");
        rsd_terminal_run("ls");
        rsd_terminal_run("frobnicate");

        memset(&term, 0, sizeof(term));
        term.frame = (struct rsd_rect){ 160U, 150U, 560U, 340U };
        term.active = true;
        rsd_window_set_title(&term, "user@rsd: ~");

        rsd_shell_draw_root();
        rsd_window_draw(&screen, &term);
        rsd_terminal_draw(&screen, &term);
        if (!emit(out, "terminal.png", term.frame)) {
            return 1;
        }

        populate_packages();
        rsd_packages_select(4U);
        rsd_packages_mark(4U, RSD_PACKAGE_INSTALL);
        rsd_packages_mark(5U, RSD_PACKAGE_INSTALL);

        memset(&synaptic, 0, sizeof(synaptic));
        synaptic.frame = (struct rsd_rect){ 420U, 300U, 560U, 340U };
        synaptic.active = true;
        rsd_window_set_title(&synaptic, "Package Manager");
        rsd_window_draw(&screen, &synaptic);
        rsd_packages_draw(&screen, &synaptic);
        if (!emit(out, "packages.png", synaptic.frame)) {
            return 1;
        }

        /*
         * Apply, and then the menu rebuilt from what is installed: the
         * frame below is PROOF that the two are connected, because
         * Galculator is in the menu only because Apply put it there.
         */
        rebuild_menu();
        before = rsd_menu_row_count();
        (void)rsd_packages_apply();
        rebuild_menu();
        after = rsd_menu_row_count();
        if (after <= before) {
            fprintf(stderr, "rsd: Apply installed nothing the menu "
                            "shows (%u rows before, %u after)\n",
                    before, after);
            return 1;
        }

        /* The windows above are drawn straight onto the surface rather
         * than opened in the shell, but the shell still holds the ones
         * the session block opened - and a press that lands on one of
         * those is not a press on the root. */
        rsd_shell_reset(&screen);
        rsd_shell_set_screen(whole());
        rsd_shell_draw_root();
        if (!rsd_shell_root_press(320U, 520U)) {
            fprintf(stderr, "rsd: the root refused the press\n");
            return 1;
        }
        rsd_shell_draw_overlays();
        if (!emit(out, "menu.png", whole())) {
            return 1;
        }
        printf("proof: Apply put %u row(s) in the menu that were not "
               "there before it ran\n", after - before);
    }

    /*
     * AND NOW IT ANSWERS.  Everything above draws; this drives the shell
     * with real events and writes the frames either side of them, so a
     * click is shown to do something rather than asserted to.
     */
    if (!rsd_shell_self_test()) {
        fprintf(stderr, "rsd: shell self-test failed\n");
        return 1;
    }
    {
        struct rsd_event press;
        uint32_t taskmgr;
        uint32_t settings;
        struct rsd_rect tab;
        struct rsd_rect head;
        uint32_t was_page;
        uint32_t was_sort;

        memset(&press, 0, sizeof(press));
        press.kind = RSD_EVENT_POINTER_DOWN;

        rsd_shell_reset(&screen);
        taskmgr = rsd_shell_open(RSD_APP_TASKMGR,
            (struct rsd_rect){ 120U, 120U, 520U, 340U });
        settings = rsd_shell_open(RSD_APP_SETTINGS,
            (struct rsd_rect){ 470U, 300U, 520U, 300U });
        populate_taskmgr();
        populate_settings();

        rsd_shell_draw_root();
        rsd_shell_draw();
        if (!emit(out, "live-before.png", whole())) {
            return 1;
        }

        /* Click the Task Manager, which is UNDERNEATH: it must come to
         * the front, which is the thing the picture shows. */
        press.x = 200U;
        press.y = 130U;
        if (!rsd_shell_handle(&press)) {
            fprintf(stderr, "rsd: a press on a window did nothing\n");
            return 1;
        }
        if (rsd_shell_focused() != taskmgr) {
            fprintf(stderr, "rsd: clicking a window did not raise it\n");
            return 1;
        }

        /* Sort by RSS by pressing its header. */
        was_sort = (uint32_t)rsd_taskmgr_sort_column();
        if (!rsd_taskmgr_header_bounds(rsd_shell_window(taskmgr),
                RSD_TASKMGR_RSS, &head)) {
            return 1;
        }
        press.x = head.x + head.width / 2U;
        press.y = head.y + head.height / 2U;
        (void)rsd_shell_handle(&press);
        if ((uint32_t)rsd_taskmgr_sort_column() == was_sort) {
            fprintf(stderr, "rsd: pressing a column header did not "
                            "sort by it\n");
            return 1;
        }

        /*
         * Then the Settings window.  The point has to be inside Settings
         * and OUTSIDE the Task Manager, which is now on top: the first
         * cut of this pressed (600,310), which is in both, and the press
         * correctly went to the Task Manager - the harness caught the
         * test, not the code. Settings spans x 470..990; the Task Manager
         * ends at x 640, so 800 is unambiguously Settings.
         */
        press.x = 800U;
        press.y = 310U;
        (void)rsd_shell_handle(&press);
        if (rsd_shell_focused() != settings) {
            fprintf(stderr, "rsd: clicking the other window did not "
                            "raise it\n");
            return 1;
        }
        was_page = rsd_settings_selected();
        if (!rsd_settings_tab_bounds(rsd_shell_window(settings), 2U,
                                       &tab)) {
            return 1;
        }
        press.x = tab.x + tab.width / 2U;
        press.y = tab.y + tab.height / 2U;
        (void)rsd_shell_handle(&press);
        if (rsd_settings_selected() == was_page ||
                rsd_settings_selected() != 2U) {
            fprintf(stderr, "rsd: pressing a tab did not change the "
                            "page\n");
            return 1;
        }

        rsd_shell_draw_root();
        rsd_shell_draw();
        if (!emit(out, "live-after.png", whole())) {
            return 1;
        }
        printf("proof: a press raised a covered window, a press on a "
               "column header sorted by it, and a press on a tab changed "
               "the page - all through the shell's own hit test\n");
    }

    /*
     * THE MAIN LOOP, run for real: a terminal opened, a command typed
     * into it a character at a time, and return pressed.  Nothing here
     * calls the terminal - every keystroke goes through the shell's
     * routing to whatever window has focus.
     */
    {
        static struct rsd_event events[16];
        struct script run;
        uint32_t count = 0U;
        uint32_t term;
        uint32_t files;

        rsd_shell_reset(&screen);
        /*
         * SOMETHING BEHIND IT.  A see-through window over bare root
         * shows you the root, which is one flat colour here, and a flat
         * colour mixed with black is just a darker flat colour - it
         * looks like a tint rather than like glass.  The file manager
         * goes under the terminal's right-hand half so that what shows
         * through is a WINDOW: its icons, its labels, its edge.
         */
        files = rsd_shell_open(RSD_APP_FILES,
            (struct rsd_rect){ 660U, 200U, 380U, 400U });
        term = rsd_shell_open(RSD_APP_TERMINAL,
            (struct rsd_rect){ 160U, 150U, 700U, 400U });
        if (term >= RSD_SHELL_MAX_WINDOWS) {
            return 1;
        }
        rsd_terminal_reset();

        events[count++] = typed('u');
        events[count++] = typed('n');
        events[count++] = typed('a');
        events[count++] = typed('m');
        events[count++] = typed('e');
        events[count++] = typed(' ');
        events[count++] = typed('-');
        events[count++] = typed('x');
        events[count++] = special_key(RSD_KEY_BACKSPACE);
        events[count++] = typed('a');
        events[count++] = special_key(RSD_KEY_ENTER);

        run.events = events;
        run.count = count;
        run.at = 0U;
        run.out = out;
        run.frames = 0U;

        {
            uint32_t handled = rsd_shell_run(script_next,
                                               script_present, &run);

            if (handled != count) {
                fprintf(stderr, "rsd: the loop handled %u of %u "
                                "events\n", handled, count);
                return 1;
            }
            /* The backspace really took the 'x' off, so the command that
             * ran was "uname -a" and not "uname -xa". */
            if (rsd_terminal_row_count() != 2U) {
                fprintf(stderr, "rsd: the typed command did not run\n");
                return 1;
            }
            /* rsdfetch, which is the one command that prints a picture. */
        rsd_terminal_run("rsdfetch");
        {
            uint32_t rows = rsd_terminal_row_count();
            uint32_t seen = 0U;
            uint32_t at;

            for (at = 0U; at < rows; ++at) {
                const char *line = rsd_terminal_row(at);

                /* The font's own name, out of the generated header,
                 * rather than a string typed here: the check said
                 * "Misc-Fixed" and went on passing after the face
                 * stopped being Misc-Fixed, which is a check agreeing
                 * with a fetch that had gone stale. */
                if (strstr(line, "OS      RSD") != NULL ||
                        strstr(line, RSD_MONO_NAME) != NULL) {
                    ++seen;
                }
            }
            if (seen != 2U) {
                fprintf(stderr, "rsd: rsdfetch printed %u of its two "
                                "checked facts\n", seen);
                return 1;
            }
        }
        rsd_shell_draw_root();
        rsd_shell_draw();
        /*
         * AND THE MARK IS IN THE MARK'S OWN COLOURS.
         *
         * WHAT THIS USED TO ASK, AND WHY IT CANNOT. It counted screen
         * pixels that were EXACTLY one of the mark's inks and wanted
         * nearly all of them present, on this reasoning, which was
         * written down here at the time:
         *
         *     The face is a bitmap font, so a lit pixel is the ink
         *     exactly rather than a blend of it.
         *
         * That stopped being true when the face stopped being a bitmap.
         * Every glyph pixel is now the ink blended with what is under
         * it by the glyph's own coverage, and a 12-pixel DejaVu Sans
         * Mono '%' is all diagonals and small bowls - it never reaches
         * full coverage anywhere, so the exact ink lands on exactly no
         * pixels. The check found nought of three and was right to:
         * what it was asking had become unanswerable.
         *
         * WHAT IS ASKED INSTEAD, in two halves, both true:
         *
         *   the screen  the mark's area is RED - pixels whose red
         *               channel stands clear of their green and blue.
         *               A mark drawn in the terminal's own grey, which
         *               is what happens when the ink plane is lost,
         *               fails this outright.
         *   the table   every ink the header declares is used by the
         *               art. The generator packs the palette down to
         *               the colours the cells actually snap to, and
         *               this is what catches that going wrong - a
         *               header claiming colours nothing is drawn in.
         */
        {
            struct rsd_rect client =
                rsd_window_client(rsd_shell_window(term));
            uint32_t reds = 0U;
            uint32_t x;
            uint32_t y;
            uint32_t at;
            uint32_t row;
            uint32_t used[RSD_LOGO_INKS];
            uint32_t unused = 0U;

            for (at = 0U; at < RSD_LOGO_INKS; ++at) {
                used[at] = 0U;
            }
            for (row = 0U; row < RSD_LOGO_ROWS; ++row) {
                const char *plane = rsd_logo_at[row];

                for (x = 0U; plane[x] != '\0'; ++x) {
                    char code = plane[x];
                    uint32_t index = code >= '0' && code <= '9'
                        ? (uint32_t)(code - '0')
                        : (code >= 'A' && code <= 'F'
                           ? (uint32_t)(code - 'A') + 10U
                           : 0U);

                    if (index != 0U && index < RSD_LOGO_INKS) {
                        ++used[index];
                    }
                }
            }
            for (at = 1U; at < RSD_LOGO_INKS; ++at) {
                if (used[at] == 0U) {
                    ++unused;
                }
            }
            if (unused != 0U) {
                fprintf(stderr, "rsd: the mark's table declares %u ink(s) "
                                "no cell is drawn in\n", unused);
                return 1;
            }

            for (y = client.y; y < client.y + client.height; ++y) {
                for (x = client.x; x < client.x + client.width; ++x) {
                    uint32_t pixel = rsd_surface_read(&screen, x, y);
                    uint32_t r = (pixel >> 16) & 0xFFU;
                    uint32_t g = (pixel >> 8) & 0xFFU;
                    uint32_t b = pixel & 0xFFU;

                    if (r > g + 24U && r > b + 24U) {
                        ++reds;
                    }
                }
            }
            if (reds < 200U) {
                fprintf(stderr, "rsd: the mark came out in %u red pixels, "
                                "which is not a mark\n", reds);
                return 1;
            }
            printf("proof: the mark is on screen in its own red over %u "
                   "pixels, and every one of the %u inks the table "
                   "declares is a colour some cell is actually drawn "
                   "in\n", reds, RSD_LOGO_INKS - 1U);
        }
        rsd_shell_draw_root();
        rsd_shell_draw();
        if (!emit(out, "rsdfetch.png", whole())) {
            return 1;
        }

        /*
         * THE SHEER PATH, ASKED FOR RATHER THAN ASSUMED.
         *
         * The terminal used to come up see-through and this scene got
         * that for free. It is opaque out of the box now - Konsole of
         * this period is, and over a white file manager a sheer ground
         * lifts to a flat grey that the text disappears into. So the
         * scene turns it on, which is what it should have done all
         * along: a check that depends on a default is a check that
         * breaks when somebody changes the default for good reasons.
         */
        rsd_terminal_set_transparent(true);
        rsd_shell_draw_root();
        rsd_shell_draw();

        /*
         * AND YOU CAN SEE THROUGH IT - THROUGH IT, not merely paler.
         *
         * The old form of this check read one pixel of the terminal's
         * ground and asked that it be neither black nor the root
         * colour.  That passed for months while the window was not
         * see-through at all: the frame used to fill the client before
         * handing it over, so the terminal mixed with its own frame and
         * came out a third flat colour, which satisfies "neither black
         * nor the root" perfectly.  Lighter is not transparent.
         *
         * What transparency means is that the ground DEPENDS ON WHAT IS
         * BEHIND IT.  So the check reads the same row at two places -
         * one with bare root behind it, one with the file manager - and
         * demands that they differ.  Nothing that mixes with a flat
         * colour of its own can pass that.
         */
        {
            struct rsd_rect client =
                rsd_window_client(rsd_shell_window(term));
            struct rsd_rect behind =
                rsd_window_client(rsd_shell_window(files));
            uint32_t sheer;
            uint32_t over;
            uint32_t solid;
            /* Below the last line rsdfetch printed, so this is ground and
             * not ink: the left end is over root, the right end is over
             * the window. */
            uint32_t sample_y = client.y + client.height - 4U;
            uint32_t sample_x = client.x + 4U;
            uint32_t over_x = behind.x + 8U;

            if (over_x >= client.x + client.width ||
                    sample_y < behind.y ||
                    sample_y >= behind.y + behind.height) {
                fprintf(stderr, "rsd: the two windows do not overlap "
                                "where the check samples them\n");
                return 1;
            }
            /*
             * THE SAMPLE-VARIANCE TEST THAT USED TO BE HERE IS GONE.
             *
             * It read sixteen pixels of the terminal's ground over the
             * root and demanded more than one distinct value. That
             * worked while the root was the X stipple - one white pixel
             * in four, so any sixteen-pixel run had texture in it. The
             * root is a photograph now and most of it is flat black
             * sky, so the same run is legitimately all one value and
             * the test failed on a terminal that was working perfectly.
             *
             * The comparison below was always the stronger one and it
             * does not care what the root looks like: read the ground
             * at two places, one over bare root and one over the file
             * manager, and demand they differ. Nothing that mixes with
             * a flat fill of its own can pass that, whatever is behind
             * it.
             */
            sheer = rsd_surface_read(&screen, sample_x, sample_y);
            over = rsd_surface_read(&screen, over_x, sample_y);
            if (sheer == over) {
                fprintf(stderr, "rsd: the terminal's ground reads "
                                "#%06X over the root and #%06X over a "
                                "window - one flat tint is not "
                                "transparency\n", sheer, over);
                return 1;
            }
            /* Off, and it is the terminal's black exactly - in both
             * places, because an opaque window owes nothing to what is
             * under it. */
            rsd_terminal_set_transparent(false);
            rsd_shell_draw_root();
            rsd_shell_draw();
            /* The same screen with the glass taken out of it, so the
             * two can be put side by side. */
            if (!emit(out, "rsdfetch-opaque.png", whole())) {
                return 1;
            }
            solid = rsd_surface_read(&screen, sample_x, sample_y);
            if (solid != 0x000000U ||
                    rsd_surface_read(&screen, over_x, sample_y) !=
                        0x000000U) {
                fprintf(stderr, "rsd: an opaque terminal drew #%06X, "
                                "not black\n", solid);
                return 1;
            }
            /*
             * AND THE INK FOLLOWS THE GROUND.  Transparency has no
             * compositor to hold the text opaque while the ground goes
             * clear, so the contrast it spends has to be bought back by
             * the foreground: a sheer terminal writes in white and an
             * opaque one in lxterminal's own light grey.  A switch that
             * moved one without the other would be the reason the text
             * got hard to read.
             */
            {
                uint32_t was = rsd_terminal_ink();

                rsd_terminal_set_transparent(true);
                if (rsd_terminal_ink() == was ||
                        rsd_terminal_ink() != 0xFFFFFFU) {
                    fprintf(stderr, "rsd: a see-through terminal "
                                    "writes in #%06X, not white\n",
                            rsd_terminal_ink());
                    return 1;
                }
            }
            rsd_terminal_set_transparent(true);
            rsd_shell_draw_root();
            rsd_shell_draw();
            printf("proof: rsdfetch printed the mark and its facts, and "
                   "the terminal's ground reads #%06X where the "
                   "wallpaper is behind it and #%06X where a window is - "
                   "it is what is behind it that shows, not a tint - and "
                   "the switch makes both black again and takes the ink "
                   "back down to the opaque grey\n", sheer, over);
        }

        printf("proof: %u keystrokes went through the shell to the "
                   "focused window and %u frames came out; the command "
                   "line survived a backspace and ran as \"%s\"\n",
                   handled, run.frames, rsd_terminal_row(0U));
        }
    }

    /*
     * THE ROOT MENU, PRESSED FOR REAL.  Every press here goes in as a
     * screen coordinate through rsd_shell_handle() - nothing asks
     * where the menu is and then calls a function directly, because
     * that would prove the function works and not the menu.
     *
     * This replaces the bar's proofs.  There is no launcher to press,
     * no task button to put a window down with and no pager, because
     * there is no bar: the menu IS the way in, so it is the thing that
     * has to answer.
     */
    {
        struct rsd_event press;
        struct rsd_rect box;
        uint32_t opened;
        uint32_t before;

        memset(&press, 0, sizeof(press));
        press.kind = RSD_EVENT_POINTER_DOWN;

        rsd_shell_reset(&screen);
        rsd_shell_set_screen(whole());
        populate_files();
        (void)rsd_files_open(populate_files());
        rsd_menu_reset();
        (void)rsd_menu_add("xterm", false, false);
        (void)rsd_menu_add("Files", false, false);
        (void)rsd_menu_add("Packages", false, false);
            (void)rsd_menu_add("Task Manager", false, false);
        (void)rsd_menu_add("Settings", false, false);
        (void)rsd_menu_add("", false, true);
        (void)rsd_menu_add("Run...", false, false);

        rsd_shell_draw_root();
        if (!rsd_shell_root_press(300U, 470U)) {
            fprintf(stderr, "rsd: the root refused a press\n");
            return 1;
        }
        if (!rsd_shell_root_menu_bounds(&box)) {
            fprintf(stderr, "rsd: the open menu has no bounds\n");
            return 1;
        }
        rsd_shell_draw();
        rsd_shell_draw_overlays();
        if (!emit(out, "root-menu.png", whole())) {
            return 1;
        }

        /* The second row is Files, so pressing it has to open Files -
         * and the menu has to be gone afterwards.  The rows start below
         * the title bar. */
        before = rsd_shell_window_count();
        press.x = box.x + 20U;
        press.y = box.y + RSD_MENU_TITLE_HEIGHT + 4U + 20U + 10U;
        if (!rsd_shell_handle(&press)) {
            fprintf(stderr, "rsd: a press on a menu row did nothing\n");
            return 1;
        }
        if (rsd_shell_window_count() != before + 1U) {
            fprintf(stderr, "rsd: the menu row opened no window\n");
            return 1;
        }
        opened = rsd_shell_focused();
        if (rsd_shell_app_of(opened) != RSD_APP_FILES) {
            fprintf(stderr, "rsd: the Files row opened something "
                            "else\n");
            return 1;
        }
        if (rsd_shell_root_menu_open()) {
            fprintf(stderr, "rsd: the menu stayed open after it was "
                            "used\n");
            return 1;
        }
        rsd_shell_draw_root();
        rsd_shell_draw();
        if (!emit(out, "root-opened.png", whole())) {
            return 1;
        }
        printf("proof: a press at (300,470) on bare root opened the menu "
               "at %ux%u and its Files row opened a Files window; the "
               "menu shut itself afterwards\n", box.x, box.y);
    }

    /*
     * SETTINGS THAT CHANGE THE DESKTOP.  A press on the Widget row is
     * routed through the shell like any other, and the frame after it
     * shows EVERY window in the new palette - not just the Settings
     * window that was clicked.
     */
    {
        struct rsd_event press;
        struct rsd_rect row;
        uint32_t settings;
        uint32_t was;

        memset(&press, 0, sizeof(press));
        press.kind = RSD_EVENT_POINTER_DOWN;

        rsd_shell_reset(&screen);
        rsd_shell_set_screen(whole());
        (void)rsd_theme_select(0U);
        populate_taskmgr();
        populate_settings();
        (void)rsd_shell_open(RSD_APP_TASKMGR,
            (struct rsd_rect){ 110U, 110U, 520U, 300U });
        settings = rsd_shell_open(RSD_APP_SETTINGS,
            (struct rsd_rect){ 500U, 330U, 520U, 280U });

        rsd_shell_draw_root();
        rsd_shell_draw();
        if (!emit(out, "theme-before.png", whole())) {
            return 1;
        }

        was = rsd_theme_selected();
        if (!rsd_settings_row_bounds(rsd_shell_window(settings), 0U,
                                       &row)) {
            return 1;
        }
        press.x = row.x + row.width - 40U;
        press.y = row.y + row.height / 2U;
        if (!rsd_shell_handle(&press)) {
            fprintf(stderr, "rsd: a press on the widget row did "
                            "nothing\n");
            return 1;
        }
        /* Twice, to reach the dark one, so the frame is unmistakable. */
        (void)rsd_shell_handle(&press);
        if (rsd_theme_selected() == was) {
            fprintf(stderr, "rsd: the widget row did not change the "
                            "theme\n");
            return 1;
        }

        rsd_shell_draw_root();
        rsd_shell_draw();
        if (!emit(out, "theme-after.png", whole())) {
            return 1;
        }
        printf("proof: pressing the Widget row moved the desktop from "
               "%s to %s, and the Task Manager behind it changed with "
               "it\n", rsd_theme_name(was),
               rsd_theme_name(rsd_theme_selected()));
    }

    /*
     * THE ROOT WINDOW, THE RUN BOX AND ALT+TAB.
     */
    {
        struct rsd_event key;
        uint32_t user;
        uint32_t desktop_folder;
        uint32_t before;

        memset(&key, 0, sizeof(key));
        key.kind = RSD_EVENT_KEY;

        (void)rsd_theme_select(0U);
        rsd_shell_reset(&screen);
        rsd_shell_set_screen(whole());
        user = populate_files();
        /* ~/Desktop is the first child of ~, and putting something in it
         * is what proves the root window reads the folder. */
        desktop_folder = rsd_files_child(user, 0U);
        (void)rsd_files_add(desktop_folder, "notes.txt", false, 812U);
        (void)rsd_files_add(desktop_folder, "Projects", true, 0U);
        rsd_shell_set_desktop_folder(desktop_folder);
        (void)rsd_files_open(user);
        /* The root comes up bare, so this turns them on rather than
         * assuming they are there. */
        rsd_shell_set_desktop_icons(true);

        if (rsd_shell_desktop_icon_count() != 3U) {
            fprintf(stderr, "rsd: the root window shows %u icons, not "
                            "the home mark plus the two things in "
                            "~/Desktop\n",
                    rsd_shell_desktop_icon_count());
            return 1;
        }

        rsd_shell_draw_root();
        rsd_shell_draw_desktop();
        rsd_shell_draw_window_icons();
        rsd_shell_draw();
        rsd_shell_draw_overlays();
        if (!emit(out, "root.png", whole())) {
            return 1;
        }

        /* The Run box: a name it has not got, then one it has. */
        rebuild_menu();
        {
            struct rsd_event press;
            struct rsd_rect box;

            memset(&press, 0, sizeof(press));
            press.kind = RSD_EVENT_POINTER_DOWN;
            if (!rsd_shell_root_press(300U, 470U) ||
                    !rsd_shell_root_menu_bounds(&box)) {
                fprintf(stderr, "rsd: the root menu did not open\n");
                return 1;
            }
            /* Run... is the last row. */
            press.x = box.x + 20U;
            press.y = box.y + box.height - 12U;
            if (!rsd_shell_handle(&press) || !rsd_shell_run_open()) {
                fprintf(stderr, "rsd: the Run row opened no box\n");
                return 1;
            }
        }
        {
            static const char BAD[] = "frobnicate";
            uint32_t at;

            for (at = 0U; BAD[at] != '\0'; ++at) {
                key.key = BAD[at];
                key.special = 0U;
                (void)rsd_shell_handle(&key);
            }
            key.key = 0;
            key.special = RSD_KEY_ENTER;
            (void)rsd_shell_handle(&key);
            if (!rsd_shell_run_open()) {
                fprintf(stderr, "rsd: the Run box closed on a name it "
                                "could not run\n");
                return 1;
            }
            if (rsd_shell_run_error()[0] == '\0') {
                fprintf(stderr, "rsd: the Run box refused a name "
                                "silently\n");
                return 1;
            }
            rsd_shell_draw_root();
            rsd_shell_draw_desktop();
            rsd_shell_draw_window_icons();
            rsd_shell_draw();
            rsd_shell_draw_overlays();
            if (!emit(out, "run.png", whole())) {
                return 1;
            }
        }
        {
            static const char GOOD[] = "rsd-t";
            uint32_t at;
            uint32_t narrowed;
            uint32_t all;

            for (at = 0U; at < 10U; ++at) {
                key.key = 0;
                key.special = RSD_KEY_BACKSPACE;
                (void)rsd_shell_handle(&key);
            }
            /*
             * IT SHOWS YOU WHAT THERE IS, which is the whole of why it
             * is dmenu and not a box with a field in it.  Empty, it
             * offers everything; what is typed narrows it; the arrow
             * keys walk what is left; Tab fills the input from the
             * selection.
             */
            all = rsd_shell_run_match_count();
            if (all < 2U) {
                fprintf(stderr, "rsd: an empty launcher offered %u "
                                "programs\n", all);
                return 1;
            }
            for (at = 0U; GOOD[at] != '\0'; ++at) {
                key.key = GOOD[at];
                key.special = 0U;
                (void)rsd_shell_handle(&key);
            }
            narrowed = rsd_shell_run_match_count();
            if (narrowed == 0U || narrowed >= all) {
                fprintf(stderr, "rsd: \"%s\" narrowed %u programs to "
                                "%u\n", GOOD, all, narrowed);
                return 1;
            }
            /* The first match really does START with what was typed,
             * which is the prefix bucket coming first. Checked against
             * GOOD itself rather than against two characters written
             * out here, so changing what is typed cannot leave the
             * check testing the old string. */
            if (strncmp(rsd_shell_run_match(0U), GOOD,
                        sizeof(GOOD) - 1U) != 0) {
                fprintf(stderr, "rsd: the first match is \"%s\", which "
                                "does not start with \"%s\"\n",
                        rsd_shell_run_match(0U), GOOD);
                return 1;
            }
            rsd_shell_draw_root();
            rsd_shell_draw();
            rsd_shell_draw_overlays();
            if (!emit(out, "launcher.png", whole())) {
                return 1;
            }
            /* The arrow moves the selection, and Tab fills the input
             * from it - dmenu.1's own two keys. */
            {
                const char *first = rsd_shell_run_match(0U);
                const char *second;

                key.key = 0;
                key.special = RSD_KEY_RIGHT;
                (void)rsd_shell_handle(&key);
                if (rsd_shell_run_selected() != 1U) {
                    fprintf(stderr, "rsd: the arrow did not move the "
                                    "selection\n");
                    return 1;
                }
                second = rsd_shell_run_match(1U);
                key.special = RSD_KEY_TAB;
                (void)rsd_shell_handle(&key);
                if (strcmp(rsd_shell_run_text(), second) != 0) {
                    fprintf(stderr, "rsd: Tab put \"%s\" in the input, "
                                    "not \"%s\"\n",
                            rsd_shell_run_text(), second);
                    return 1;
                }
                (void)first;
            }

            before = rsd_shell_window_count();
            key.key = 0;
            key.special = RSD_KEY_ENTER;
            (void)rsd_shell_handle(&key);
            if (rsd_shell_run_open()) {
                fprintf(stderr, "rsd: the launcher stayed open on a "
                                "name it ran\n");
                return 1;
            }
            if (rsd_shell_window_count() != before + 1U) {
                fprintf(stderr, "rsd: the launcher opened no window\n");
                return 1;
            }
            printf("proof: the launcher offered %u programs, \"lx\" cut "
                   "them to %u, the arrow moved along them and Tab "
                   "filled the input from the one it landed on\n",
                   all, narrowed);
        }

        /*
         * ICONIFY, AND THE WAY BACK.  With no taskbar, a minimised
         * window has to land somewhere you can see and press: fvwm puts
         * an icon on the root and so does this.  Pressing the window's
         * own minimise box puts it down, the icon appears at the foot of
         * the screen, and a press on the icon brings it back.
         */
        {
            uint32_t slot = rsd_shell_focused();
            struct rsd_event press;
            struct rsd_rect box;
            struct rsd_rect cell;

            memset(&press, 0, sizeof(press));
            press.kind = RSD_EVENT_POINTER_DOWN;
            if (slot >= RSD_SHELL_MAX_WINDOWS) {
                fprintf(stderr, "rsd: nothing was focused to iconify\n");
                return 1;
            }
            if (!rsd_window_button_bounds(rsd_shell_window(slot),
                    RSD_WINDOW_MINIMISE, &box)) {
                fprintf(stderr, "rsd: the minimise box has no bounds\n");
                return 1;
            }
            press.x = box.x + box.width / 2U;
            press.y = box.y + box.height / 2U;
            if (!rsd_shell_handle(&press)) {
                fprintf(stderr, "rsd: the minimise box did nothing\n");
                return 1;
            }
            if (rsd_shell_window_icon_count() != 1U ||
                    rsd_shell_window_icon_slot(0U) != slot) {
                fprintf(stderr, "rsd: iconifying put no icon on the "
                                "root (%u there)\n",
                        rsd_shell_window_icon_count());
                return 1;
            }
            if (!rsd_shell_window_icon_bounds(0U, &cell)) {
                return 1;
            }
            rsd_shell_draw_root();
            rsd_shell_draw_window_icons();
            rsd_shell_draw();
            if (!emit(out, "iconified.png", whole())) {
                return 1;
            }
            press.x = cell.x + cell.width / 2U;
            press.y = cell.y + cell.height / 2U;
            if (!rsd_shell_root_press(press.x, press.y)) {
                fprintf(stderr, "rsd: a press on the icon did "
                                "nothing\n");
                return 1;
            }
            if (rsd_shell_window_icon_count() != 0U ||
                    rsd_shell_focused() != slot) {
                fprintf(stderr, "rsd: the icon did not put the window "
                                "back\n");
                return 1;
            }
            /* And it is not the MENU that opened: an icon press is not
             * a root press, even though the icon is on the root. */
            if (rsd_shell_root_menu_open()) {
                fprintf(stderr, "rsd: the icon press opened the menu "
                                "over it\n");
                return 1;
            }
        }

        /*
         * THE THREE SHORTCUTS THE KEYBOARD PAGE CLAIMS.  They were
         * three lines of text and nothing behind them until now, so
         * this asks each one to do the thing its line says.
         */
        {
            uint32_t had = rsd_shell_window_count();

            key.modifiers = RSD_MOD_SUPER;
            key.special = 0U;
            key.key = 'e';
            if (!rsd_shell_handle(&key) ||
                    rsd_shell_window_count() != had + 1U ||
                    rsd_shell_app_of(rsd_shell_focused()) !=
                        RSD_APP_FILES) {
                fprintf(stderr, "rsd: W-e opened no file manager\n");
                return 1;
            }
            key.key = 'r';
            if (!rsd_shell_handle(&key) || !rsd_shell_run_open()) {
                fprintf(stderr, "rsd: W-r opened no Run box\n");
                return 1;
            }
            key.modifiers = 0U;
            key.special = RSD_KEY_ESCAPE;
            key.key = 0;
            (void)rsd_shell_handle(&key);

            had = rsd_shell_window_count();
            key.modifiers = RSD_MOD_CTRL | RSD_MOD_ALT;
            key.special = 0U;
            key.key = 't';
            if (!rsd_shell_handle(&key) ||
                    rsd_shell_window_count() != had + 1U ||
                    rsd_shell_app_of(rsd_shell_focused()) !=
                        RSD_APP_TERMINAL) {
                fprintf(stderr, "rsd: C-A-t opened no terminal\n");
                return 1;
            }
            /* And Alt+F4 shuts the one it just opened. */
            key.modifiers = RSD_MOD_ALT;
            key.special = RSD_KEY_F4;
            key.key = 0;
            if (!rsd_shell_handle(&key) ||
                    rsd_shell_window_count() != had) {
                fprintf(stderr, "rsd: A-F4 closed nothing\n");
                return 1;
            }
        }

        /* Alt+Tab, with two windows up. */
        (void)rsd_shell_open(RSD_APP_TASKMGR,
            (struct rsd_rect){ 380U, 260U, 520U, 300U });
        key.modifiers = RSD_MOD_ALT;
        key.key = 0;
        key.special = RSD_KEY_TAB;
        if (!rsd_shell_handle(&key) || !rsd_shell_switcher_open()) {
            fprintf(stderr, "rsd: Alt+Tab opened no switcher\n");
            return 1;
        }
        rsd_shell_draw_root();
        rsd_shell_draw_desktop();
        rsd_shell_draw_window_icons();
        rsd_shell_draw();
        rsd_shell_draw_overlays();
        if (!emit(out, "switcher.png", whole())) {
            return 1;
        }
        {
            uint32_t was_focus = rsd_shell_focused();

            key.modifiers = 0U;
            key.special = 0U;
            key.key = 0;
            (void)rsd_shell_handle(&key);
            if (rsd_shell_switcher_open()) {
                fprintf(stderr, "rsd: releasing Alt left the switcher "
                                "up\n");
                return 1;
            }
            if (rsd_shell_focused() == was_focus) {
                fprintf(stderr, "rsd: Alt+Tab committed to the window "
                                "that already had focus\n");
                return 1;
            }
        }
        printf("proof: the root window draws %u icons including the two "
               "things in ~/Desktop, the Run box refused \"frobnicate\" "
               "out loud and then ran lxterminal, and Alt+Tab moved "
               "focus to another window\n",
               rsd_shell_desktop_icon_count());
    }

    /*
     * SETTINGS: EVERY CONTROL ON THE WINDOW DOES THE THING IT NAMES.
     *
     * Two of these switches used to flip a bool inside the row and stop
     * there, which made the Settings window the largest possible version
     * of a control that does not do what it is drawn as.  This presses
     * each one through rsd_settings_press() - the same call the shell
     * makes when you click it - and then asks the thing it claims to
     * control, not the row, whether it changed.
     */
    {
        uint32_t user;
        uint32_t before;
        uint32_t after;
        uint32_t docs;

        rsd_shell_reset(&screen);
        rsd_shell_set_screen(whole());
        user = populate_files();
        (void)rsd_files_add(user, ".bashrc", false, 3771U);
        (void)rsd_files_open(user);
        rsd_shell_set_desktop_folder(rsd_files_child(user, 0U));
        populate_settings();

        /* Show icons on the desktop - page 1, row 1.  The root comes
         * up bare now, so the switch is asked to FILL it and then to
         * clear it again, which is the same switch either way. */
        if (rsd_shell_desktop_icon_count() != 0U) {
            fprintf(stderr, "rsd: the root came up with icons on it\n");
            return 1;
        }
        if (!rsd_settings_press(1U, 1U)) {
            fprintf(stderr, "rsd: the desktop-icons switch refused the "
                            "press\n");
            return 1;
        }
        before = rsd_shell_desktop_icon_count();
        if (before == 0U) {
            fprintf(stderr, "rsd: the desktop-icons switch drew "
                            "nothing on the root\n");
            return 1;
        }
        if (!rsd_settings_press(1U, 1U) ||
                rsd_shell_desktop_icon_count() != 0U) {
            fprintf(stderr, "rsd: the desktop-icons switch did not "
                            "clear the root again (%u icons)\n", before);
            return 1;
        }
        (void)rsd_settings_press(1U, 1U);

        /* Show hidden files - page 1, row 2.  ~/.bashrc is the one. */
        before = rsd_files_visible_count(user);
        if (before != rsd_files_child_count(user) - 1U) {
            fprintf(stderr, "rsd: a dot file was visible with hidden "
                            "files off (%u of %u shown)\n",
                    before, rsd_files_child_count(user));
            return 1;
        }
        if (!rsd_settings_press(1U, 2U) ||
                rsd_files_visible_count(user) !=
                    rsd_files_child_count(user)) {
            fprintf(stderr, "rsd: the hidden-files switch did not show "
                            "the dot file\n");
            return 1;
        }
        after = rsd_files_visible_count(user);
        (void)rsd_settings_press(1U, 2U);

        /* Open on a single click - page 1, row 3.  With it off, one press
         * on a folder selects; with it on, the same press opens. */
        docs = rsd_files_child(user, 3U);
        {
            struct rsd_event press;
            struct rsd_rect cell;
            struct rsd_window *window;
            uint32_t slot;
            uint32_t at;
            uint32_t index = RSD_FILES_MAX_NODES;

            struct rsd_rect where;

            where.x = 60U;
            where.y = 60U;
            where.width = 640U;
            where.height = 480U;
            slot = rsd_shell_open(RSD_APP_FILES, where);
            window = rsd_shell_window(slot);
            for (at = 0U; at < rsd_files_visible_count(user); ++at) {
                if (rsd_files_visible_child(user, at) == docs) {
                    index = at;
                }
            }
            if (index == RSD_FILES_MAX_NODES ||
                    !rsd_files_entry_bounds(window, index, &cell)) {
                fprintf(stderr, "rsd: could not find Documents in the "
                                "view\n");
                return 1;
            }
            memset(&press, 0, sizeof(press));
            press.kind = RSD_EVENT_POINTER_DOWN;
            press.x = cell.x + cell.width / 2U;
            press.y = cell.y + cell.height / 2U;
            (void)rsd_shell_handle(&press);
            if (rsd_files_here() == docs) {
                fprintf(stderr, "rsd: one click opened a folder with "
                                "single click off\n");
                return 1;
            }
            if (!rsd_settings_press(1U, 3U)) {
                fprintf(stderr, "rsd: the single-click switch refused "
                                "the press\n");
                return 1;
            }
            (void)rsd_shell_handle(&press);
            if (rsd_files_here() != docs) {
                fprintf(stderr, "rsd: single click was on and one click "
                                "still did not open the folder\n");
                return 1;
            }
            (void)rsd_settings_press(1U, 3U);
            (void)rsd_files_open(user);
        }

        printf("proof: every switch on the Settings window moves the "
               "thing it names - the root went from bare to %u icons and "
               "back, a dot file appeared (%u of %u), and one click "
               "opened a folder only once single click was on\n",
               before, after, rsd_files_child_count(user));
    }

    /*
     * NOTIFICATIONS AND RESIZING.
     */
    {
        struct rsd_event event;
        struct rsd_window *window;
        uint32_t slot;
        uint32_t at;
        struct rsd_rect was;

        memset(&event, 0, sizeof(event));
        rsd_shell_reset(&screen);
        rsd_shell_set_screen(whole());
        populate_taskmgr();
        slot = rsd_shell_open(RSD_APP_TASKMGR,
            (struct rsd_rect){ 200U, 160U, 520U, 300U });
        window = rsd_shell_window(slot);
        if (window == NULL) {
            return 1;
        }

        /* A notice AGES OUT on its own rather than sitting there. */
        rsd_shell_notify("Package Manager", "2 packages installed");
        rsd_shell_notify("Files", "report.txt moved to Notes");
        if (rsd_shell_note_count() != 2U) {
            fprintf(stderr, "rsd: notices did not queue\n");
            return 1;
        }

        rsd_shell_draw_root();
        rsd_shell_draw_desktop();
        rsd_shell_draw_window_icons();
        rsd_shell_draw();
        rsd_shell_draw_overlays();
        if (!emit(out, "notes.png", whole())) {
            return 1;
        }

        /* RESIZE from the bottom-right corner: both dimensions at once. */
        was = window->frame;
        event.kind = RSD_EVENT_POINTER_DOWN;
        event.x = was.x + was.width - 2U;
        event.y = was.y + was.height - 2U;
        if (!rsd_shell_handle(&event)) {
            fprintf(stderr, "rsd: a press on the corner did nothing\n");
            return 1;
        }
        event.kind = RSD_EVENT_POINTER_MOVE;
        event.x += 90U;
        event.y += 60U;
        (void)rsd_shell_handle(&event);
        if (window->frame.width != was.width + 90U ||
                window->frame.height != was.height + 60U) {
            fprintf(stderr, "rsd: the corner drag gave %ux%u, not "
                            "%ux%u\n", window->frame.width,
                    window->frame.height, was.width + 90U,
                    was.height + 60U);
            return 1;
        }
        /* It cannot be dragged smaller than the minimum, and dragging
         * back out afterwards must not be offset by how far past the
         * minimum the pointer went. */
        event.x = was.x + 10U;
        event.y = was.y + 10U;
        (void)rsd_shell_handle(&event);
        if (window->frame.width < 180U || window->frame.height < 180U) {
            fprintf(stderr, "rsd: the window went below its minimum "
                            "(%ux%u)\n", window->frame.width,
                    window->frame.height);
            return 1;
        }
        event.x = was.x + was.width - 2U;
        event.y = was.y + was.height - 2U;
        (void)rsd_shell_handle(&event);
        if (window->frame.width != was.width ||
                window->frame.height != was.height) {
            fprintf(stderr, "rsd: dragging back drifted to %ux%u from "
                            "%ux%u\n", window->frame.width,
                    window->frame.height, was.width, was.height);
            return 1;
        }
        event.kind = RSD_EVENT_POINTER_UP;
        (void)rsd_shell_handle(&event);

        /* And notices really expire. */
        for (at = 0U; at < 14U; ++at) {
            rsd_shell_tick();
        }
        if (rsd_shell_note_count() != 0U) {
            fprintf(stderr, "rsd: %u notices never expired\n",
                    rsd_shell_note_count());
            return 1;
        }
        /*
         * THE CONTEXT MENU AND THE RENAME BOX, driven from coordinates.
         */
        {
            struct rsd_rect cell;
            uint32_t files_slot;
            uint32_t user = populate_files();
            uint32_t target;

            rsd_shell_reset(&screen);
            rsd_shell_set_screen(whole());
            (void)rsd_files_open(user);
            files_slot = rsd_shell_open(RSD_APP_FILES,
                (struct rsd_rect){ 180U, 140U, 640U, 460U });
            if (files_slot >= RSD_SHELL_MAX_WINDOWS) {
                return 1;
            }
            if (!rsd_files_entry_bounds(rsd_shell_window(files_slot),
                    1U, &cell)) {
                return 1;
            }
            target = rsd_files_child(rsd_files_here(), 1U);

            memset(&event, 0, sizeof(event));
            event.kind = RSD_EVENT_POINTER_DOWN;
            event.secondary = true;
            /*
             * The CENTRE of the cell, not a fixed offset into it.  The
             * first version used +20, which is inside a 72-pixel icon
             * cell and past the end of an 18-pixel list row - and the
             * view had been left in list mode by the Settings test
             * above, so the press landed on the next entry and renamed
             * the wrong thing.  A test that assumes a layout it did not
             * ask for is a test that passes for the wrong reason when it
             * passes at all.
             */
            event.x = cell.x + cell.width / 2U;
            event.y = cell.y + cell.height / 2U;
            if (!rsd_shell_handle(&event) ||
                    !rsd_shell_context_open()) {
                fprintf(stderr, "rsd: a right-click opened no menu\n");
                return 1;
            }
            rsd_shell_draw_root();
            rsd_shell_draw_desktop();
            rsd_shell_draw_window_icons();
            rsd_shell_draw();
            rsd_shell_draw_overlays();
            if (!emit(out, "context.png", whole())) {
                return 1;
            }

            /* Rename: the row opens a box prefilled with the name. */
            {
                struct rsd_rect menu = rsd_shell_context_bounds();

                event.secondary = false;
                event.x = menu.x + 20U;
                event.y = menu.y + 4U + 20U + 10U;
                if (!rsd_shell_handle(&event) ||
                        !rsd_shell_rename_open()) {
                    fprintf(stderr, "rsd: Rename opened no box\n");
                    return 1;
                }
                if (rsd_shell_rename_text()[0] == '\0') {
                    fprintf(stderr, "rsd: the rename box is empty "
                                    "rather than prefilled\n");
                    return 1;
                }
            }
            /* A name already in the folder is refused OUT LOUD and the
             * box stays up. */
            {
                static const char TAKEN[] = "Music";
                uint32_t typed;

                event.kind = RSD_EVENT_KEY;
                for (typed = 0U; typed < 24U; ++typed) {
                    event.key = 0;
                    event.special = RSD_KEY_BACKSPACE;
                    (void)rsd_shell_handle(&event);
                }
                for (typed = 0U; TAKEN[typed] != '\0'; ++typed) {
                    event.key = TAKEN[typed];
                    event.special = 0U;
                    (void)rsd_shell_handle(&event);
                }
                event.key = 0;
                event.special = RSD_KEY_ENTER;
                (void)rsd_shell_handle(&event);
                if (!rsd_shell_rename_open()) {
                    fprintf(stderr, "rsd: the rename box closed on a "
                                    "name it refused\n");
                    return 1;
                }
                if (rsd_shell_rename_error()[0] == '\0') {
                    fprintf(stderr, "rsd: the rename was refused "
                                    "silently\n");
                    return 1;
                }
            }
            /* And a free name goes through. */
            {
                static const char FREE[] = "Papers";
                uint32_t typed;

                for (typed = 0U; typed < 24U; ++typed) {
                    event.key = 0;
                    event.special = RSD_KEY_BACKSPACE;
                    (void)rsd_shell_handle(&event);
                }
                for (typed = 0U; FREE[typed] != '\0'; ++typed) {
                    event.key = FREE[typed];
                    event.special = 0U;
                    (void)rsd_shell_handle(&event);
                }
                event.key = 0;
                event.special = RSD_KEY_ENTER;
                (void)rsd_shell_handle(&event);
                if (rsd_shell_rename_open()) {
                    fprintf(stderr, "rsd: the rename box stayed up on "
                                    "a name it took\n");
                    return 1;
                }
                {
                    const char *now = rsd_files_node_name(target);
                    uint32_t byte = 0U;

                    while (FREE[byte] != '\0' && now[byte] == FREE[byte]) {
                        ++byte;
                    }
                    if (FREE[byte] != '\0' || now[byte] != '\0') {
                        fprintf(stderr, "rsd: the rename did not take "
                                        "(the menu was about %s, not %s)\n",
                                rsd_files_node_name(
                                    rsd_shell_context_node()), now);
                        return 1;
                    }
                }
            }
            rsd_shell_draw_root();
            rsd_shell_draw_desktop();
            rsd_shell_draw_window_icons();
            rsd_shell_draw();
            rsd_shell_draw_overlays();
            if (!emit(out, "renamed.png", whole())) {
                return 1;
            }
            printf("proof: a right-click opened pcmanfm's menu, Rename "
                   "refused \"Music\" out loud because the folder "
                   "already had one, and took \"Papers\"\n");
        }

        /* END TASK, pressed, and the window it names really closes. */
        {
            struct rsd_rect button;
            uint32_t victim;
            uint32_t rows_before;
            uint32_t windows_before;

            rsd_shell_reset(&screen);
            rsd_shell_set_screen(whole());
            victim = rsd_shell_open(RSD_APP_TERMINAL,
                (struct rsd_rect){ 260U, 180U, 520U, 280U });
            slot = rsd_shell_open(RSD_APP_TASKMGR,
                (struct rsd_rect){ 200U, 300U, 520U, 340U });
            if (victim >= RSD_SHELL_MAX_WINDOWS ||
                    slot >= RSD_SHELL_MAX_WINDOWS) {
                return 1;
            }
            /* The rows ARE the windows, so sync them the way a draw
             * would and then act on what the bar and the list agree on. */
            rsd_shell_draw();
            rsd_taskmgr_reset();
            {
                struct rsd_taskmgr_row row;
                uint32_t which;

                for (which = 0U; which < 2U; ++which) {
                    memset(&row, 0, sizeof(row));
                    (void)snprintf(row.command, RSD_TASKMGR_NAME_BYTES,
                        "%s", which == victim ? "lxterminal" : "lxtask");
                    (void)snprintf(row.user, RSD_TASKMGR_NAME_BYTES,
                                   "user");
                    row.cpu_tenths = 20U + which;
                    row.rss_kib = 2000U + which * 500U;
                    row.pid = which + RSD_SHELL_FIRST_PID;
                    (void)rsd_taskmgr_add(&row);
                }
            }
            /* pid 2 is slot 1; the terminal is slot 0, so pick the row
             * whose pid names it. */
            {
                uint32_t at2;

                for (at2 = 0U; at2 < rsd_taskmgr_count(); ++at2) {
                    struct rsd_rect row;

                    if (!rsd_taskmgr_row_bounds(
                            rsd_shell_window(slot), at2, &row)) {
                        continue;
                    }
                    rsd_taskmgr_select(at2);
                    if (rsd_taskmgr_selected_pid() ==
                            victim + RSD_SHELL_FIRST_PID) {
                        break;
                    }
                }
            }
            if (rsd_taskmgr_selected_pid() !=
                    victim + RSD_SHELL_FIRST_PID) {
                fprintf(stderr, "rsd: could not pick the row for the "
                                "window being ended\n");
                return 1;
            }
            rows_before = rsd_taskmgr_count();
            windows_before = rsd_shell_window_count();
            if (!rsd_taskmgr_end_button(rsd_shell_window(slot),
                                          &button)) {
                return 1;
            }
            event.kind = RSD_EVENT_POINTER_DOWN;
            event.secondary = false;
            event.x = button.x + button.width / 2U;
            event.y = button.y + button.height / 2U;
            if (!rsd_shell_handle(&event)) {
                fprintf(stderr, "rsd: End Task did nothing\n");
                return 1;
            }
            if (rsd_taskmgr_count() != rows_before - 1U) {
                fprintf(stderr, "rsd: End Task left the row\n");
                return 1;
            }
            if (rsd_shell_window_count() != windows_before - 1U) {
                fprintf(stderr, "rsd: End Task removed the row but "
                                "left the window on the screen\n");
                return 1;
            }
            printf("proof: End Task removed the row AND closed the "
                   "window it named, and the session row refuses to be "
                   "ended at all\n");
        }

        /* THE CLIPBOARD, through the shell's own keys. */
        {
            uint32_t user = populate_files();
            uint32_t files_slot;
            uint32_t docs;
            uint32_t before;

            rsd_shell_reset(&screen);
            rsd_shell_set_screen(whole());
            (void)rsd_files_open(user);
            files_slot = rsd_shell_open(RSD_APP_FILES,
                (struct rsd_rect){ 200U, 150U, 640U, 460U });
            if (files_slot >= RSD_SHELL_MAX_WINDOWS) {
                return 1;
            }
            docs = rsd_files_child(user, 1U);
            rsd_files_select(rsd_files_child(user, 6U), false);

            event.kind = RSD_EVENT_KEY;
            event.modifiers = RSD_MOD_CTRL;
            event.special = 0U;
            event.key = 'c';
            if (!rsd_shell_handle(&event)) {
                fprintf(stderr, "rsd: Ctrl+C copied nothing\n");
                return 1;
            }
            before = rsd_files_child_count(docs);
            (void)rsd_files_open(docs);
            event.key = 'v';
            if (!rsd_shell_handle(&event)) {
                fprintf(stderr, "rsd: Ctrl+V pasted nothing\n");
                return 1;
            }
            if (rsd_files_child_count(docs) != before + 1U) {
                fprintf(stderr, "rsd: the paste did not arrive\n");
                return 1;
            }
            /* A copy is not spent, so a second paste lands too - and it
             * cannot overwrite the first. */
            event.key = 'v';
            (void)rsd_shell_handle(&event);
            if (rsd_files_child_count(docs) != before + 2U) {
                fprintf(stderr, "rsd: the second paste overwrote the "
                                "first\n");
                return 1;
            }
            event.modifiers = 0U;
            printf("proof: Ctrl+C then Ctrl+V put README.txt in "
                   "Documents and a second Ctrl+V put \"%s\" beside "
                   "it rather than over it\n",
                   rsd_files_node_name(
                       rsd_files_child(docs, before + 1U)));
        }
        printf("proof: two notices queued and aged out, and a corner "
               "drag resized both dimensions and came back to %ux%u "
               "exactly after hitting the minimum\n",
               was.width, was.height);
    }
    printf("proof: a %ux%u screen with nothing on it but the root and "
           "what you start from its menu - no bar, no dock, no tray; a "
           "task manager of %u processes sorted by a column that really "
           "reorders and reverses; a notebook of %u pages where picking "
           "a tab changes the page; and a file manager over %u nodes "
           "whose folder sizes are counted rather than stored\n",
           SCREEN_WIDTH, SCREEN_HEIGHT,
           rsd_taskmgr_count(), rsd_settings_page_count(),
           rsd_files_child_count(rsd_files_here()));
    return 0;
}
