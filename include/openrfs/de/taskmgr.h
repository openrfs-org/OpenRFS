/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DE_TASKMGR_H
#define OPENRFS_DE_TASKMGR_H

#include <stdbool.h>
#include <stdint.h>

#include <openrfs/de/surface.h>
#include <openrfs/de/window.h>

/*
 * lxtask, the OpenRFS task manager.
 *
 * Its window is a menu bar, a one-line summary, a column header and a
 * list: Command, User, CPU%, RSS, PID, in that order, with an End Task
 * button under them.  A row is a PROCESS rather than a window, which is
 * why the column is Command and not Title - lxtask lists what is running,
 * and the first cut of the JavaScript beside this listed window titles
 * and was wrong for exactly that reason.
 *
 * Clicking a column header sorts by it, which lxtask does and which is
 * the reason the sort key lives here rather than in the drawing code.
 */

#define OPENRFS_TASKMGR_MAX_ROWS 24U
#define OPENRFS_TASKMGR_NAME_BYTES 32U
#define OPENRFS_TASKMGR_COLUMNS 5U

enum openrfs_taskmgr_column {
    OPENRFS_TASKMGR_COMMAND = 0,
    OPENRFS_TASKMGR_USER,
    OPENRFS_TASKMGR_CPU,
    OPENRFS_TASKMGR_RSS,
    OPENRFS_TASKMGR_PID
};

struct openrfs_taskmgr_row {
    char command[OPENRFS_TASKMGR_NAME_BYTES];
    char user[OPENRFS_TASKMGR_NAME_BYTES];
    uint32_t cpu_tenths;     /* 42 = 4.2%, because there is no float */
    uint32_t rss_kib;
    uint32_t pid;
};

void openrfs_taskmgr_reset(void);
bool openrfs_taskmgr_add(const struct openrfs_taskmgr_row *row);
uint32_t openrfs_taskmgr_count(void);

/* Sorting by the column already sorted on reverses it, which is what
 * every list with clickable headers does. */
/*
 * THE SELECTION, AND END TASK.
 *
 * The button was drawn and did nothing, which is the one thing this
 * desktop does not do.  Ending a task removes the row; whether that also
 * closes a window is the shell's business, because the Task Manager does
 * not know windows exist.
 */
void openrfs_taskmgr_select(uint32_t at);
uint32_t openrfs_taskmgr_selected(void);
bool openrfs_taskmgr_has_selection(void);
uint32_t openrfs_taskmgr_selected_pid(void);
/* Removes the selected row.  Refuses pid 1 - the session itself - the
 * way a task manager refuses to kill what it is running inside. */
bool openrfs_taskmgr_end_selected(void);
bool openrfs_taskmgr_row_bounds(const struct openrfs_window *window,
    uint32_t at, struct openrfs_rect *out);
bool openrfs_taskmgr_end_button(const struct openrfs_window *window,
    struct openrfs_rect *out);

void openrfs_taskmgr_sort(enum openrfs_taskmgr_column column);
enum openrfs_taskmgr_column openrfs_taskmgr_sort_column(void);
bool openrfs_taskmgr_sort_descending(void);

/* Where a header sits, so a press can be turned into a column without
 * the caller knowing the layout. */
bool openrfs_taskmgr_header_bounds(const struct openrfs_window *window,
    enum openrfs_taskmgr_column column, struct openrfs_rect *out);

void openrfs_taskmgr_draw(struct openrfs_surface *surface,
    const struct openrfs_window *window);

bool openrfs_taskmgr_self_test(void);

#endif /* OPENRFS_DE_TASKMGR_H */
