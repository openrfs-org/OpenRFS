/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_TASKMGR_H
#define TRAIT_TASKMGR_H

#include <stdbool.h>
#include <stdint.h>

#include <trait/surface.h>
#include <trait/window.h>

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

#define TRAIT_TASKMGR_MAX_ROWS 24U
#define TRAIT_TASKMGR_NAME_BYTES 32U
#define TRAIT_TASKMGR_COLUMNS 5U

enum trait_taskmgr_column {
    TRAIT_TASKMGR_COMMAND = 0,
    TRAIT_TASKMGR_USER,
    TRAIT_TASKMGR_CPU,
    TRAIT_TASKMGR_RSS,
    TRAIT_TASKMGR_PID
};

struct trait_taskmgr_row {
    char command[TRAIT_TASKMGR_NAME_BYTES];
    char user[TRAIT_TASKMGR_NAME_BYTES];
    uint32_t cpu_tenths;     /* 42 = 4.2%, because there is no float */
    uint32_t rss_kib;
    uint32_t pid;
};

void trait_taskmgr_reset(void);
bool trait_taskmgr_add(const struct trait_taskmgr_row *row);
uint32_t trait_taskmgr_count(void);

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
void trait_taskmgr_select(uint32_t at);
uint32_t trait_taskmgr_selected(void);
bool trait_taskmgr_has_selection(void);
uint32_t trait_taskmgr_selected_pid(void);
/* Removes the selected row.  Refuses pid 1 - the session itself - the
 * way a task manager refuses to kill what it is running inside. */
bool trait_taskmgr_end_selected(void);
bool trait_taskmgr_row_bounds(const struct trait_window *window,
    uint32_t at, struct trait_rect *out);
bool trait_taskmgr_end_button(const struct trait_window *window,
    struct trait_rect *out);

void trait_taskmgr_sort(enum trait_taskmgr_column column);
enum trait_taskmgr_column trait_taskmgr_sort_column(void);
bool trait_taskmgr_sort_descending(void);

/* Where a header sits, so a press can be turned into a column without
 * the caller knowing the layout. */
bool trait_taskmgr_header_bounds(const struct trait_window *window,
    enum trait_taskmgr_column column, struct trait_rect *out);

void trait_taskmgr_draw(struct trait_surface *surface,
    const struct trait_window *window);

bool trait_taskmgr_self_test(void);

#endif /* TRAIT_TASKMGR_H */
