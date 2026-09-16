/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_DE_TASKMGR_H
#define OPENGAT_DE_TASKMGR_H

#include <stdbool.h>
#include <stdint.h>

#include <opengat/de/surface.h>
#include <opengat/de/window.h>

/*
 * lxtask, the OpenGAT task manager.
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

#define OPENGAT_TASKMGR_MAX_ROWS 24U
#define OPENGAT_TASKMGR_NAME_BYTES 32U
#define OPENGAT_TASKMGR_COLUMNS 5U

enum opengat_taskmgr_column {
    OPENGAT_TASKMGR_COMMAND = 0,
    OPENGAT_TASKMGR_USER,
    OPENGAT_TASKMGR_CPU,
    OPENGAT_TASKMGR_RSS,
    OPENGAT_TASKMGR_PID
};

struct opengat_taskmgr_row {
    char command[OPENGAT_TASKMGR_NAME_BYTES];
    char user[OPENGAT_TASKMGR_NAME_BYTES];
    uint32_t cpu_tenths;     /* 42 = 4.2%, because there is no float */
    uint32_t rss_kib;
    uint32_t pid;
};

void opengat_taskmgr_reset(void);
bool opengat_taskmgr_add(const struct opengat_taskmgr_row *row);
uint32_t opengat_taskmgr_count(void);

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
void opengat_taskmgr_select(uint32_t at);
uint32_t opengat_taskmgr_selected(void);
bool opengat_taskmgr_has_selection(void);
uint32_t opengat_taskmgr_selected_pid(void);
/* Removes the selected row.  Refuses pid 1 - the session itself - the
 * way a task manager refuses to kill what it is running inside. */
bool opengat_taskmgr_end_selected(void);
bool opengat_taskmgr_row_bounds(const struct opengat_window *window,
    uint32_t at, struct opengat_rect *out);
bool opengat_taskmgr_end_button(const struct opengat_window *window,
    struct opengat_rect *out);

void opengat_taskmgr_sort(enum opengat_taskmgr_column column);
enum opengat_taskmgr_column opengat_taskmgr_sort_column(void);
bool opengat_taskmgr_sort_descending(void);

/* Where a header sits, so a press can be turned into a column without
 * the caller knowing the layout. */
bool opengat_taskmgr_header_bounds(const struct opengat_window *window,
    enum opengat_taskmgr_column column, struct opengat_rect *out);

void opengat_taskmgr_draw(struct opengat_surface *surface,
    const struct opengat_window *window);

bool opengat_taskmgr_self_test(void);

#endif /* OPENGAT_DE_TASKMGR_H */
