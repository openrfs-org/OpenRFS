/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_TASKMGR_H
#define RSD_TASKMGR_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd_desktop/surface.h>
#include <rsd_desktop/window.h>

/*
 * lxtask, which is LXDE's task manager.
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

#define RSD_TASKMGR_MAX_ROWS 24U
#define RSD_TASKMGR_NAME_BYTES 32U
#define RSD_TASKMGR_COLUMNS 5U

enum rsd_taskmgr_column {
    RSD_TASKMGR_COMMAND = 0,
    RSD_TASKMGR_USER,
    RSD_TASKMGR_CPU,
    RSD_TASKMGR_RSS,
    RSD_TASKMGR_PID
};

struct rsd_taskmgr_row {
    char command[RSD_TASKMGR_NAME_BYTES];
    char user[RSD_TASKMGR_NAME_BYTES];
    uint32_t cpu_tenths;     /* 42 = 4.2%, because there is no float */
    uint32_t rss_kib;
    uint32_t pid;
};

void rsd_taskmgr_reset(void);
bool rsd_taskmgr_add(const struct rsd_taskmgr_row *row);
uint32_t rsd_taskmgr_count(void);

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
void rsd_taskmgr_select(uint32_t at);
uint32_t rsd_taskmgr_selected(void);
bool rsd_taskmgr_has_selection(void);
uint32_t rsd_taskmgr_selected_pid(void);
/* Removes the selected row.  Refuses pid 1 - the session itself - the
 * way a task manager refuses to kill what it is running inside. */
bool rsd_taskmgr_end_selected(void);
bool rsd_taskmgr_row_bounds(const struct rsd_window *window,
    uint32_t at, struct rsd_rect *out);
bool rsd_taskmgr_end_button(const struct rsd_window *window,
    struct rsd_rect *out);

void rsd_taskmgr_sort(enum rsd_taskmgr_column column);
enum rsd_taskmgr_column rsd_taskmgr_sort_column(void);
bool rsd_taskmgr_sort_descending(void);

/* Where a header sits, so a press can be turned into a column without
 * the caller knowing the layout. */
bool rsd_taskmgr_header_bounds(const struct rsd_window *window,
    enum rsd_taskmgr_column column, struct rsd_rect *out);

void rsd_taskmgr_draw(struct rsd_surface *surface,
    const struct rsd_window *window);

bool rsd_taskmgr_self_test(void);

#endif /* RSD_TASKMGR_H */
