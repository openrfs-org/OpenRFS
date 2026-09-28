/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_FILES_H
#define RSD_FILES_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd_desktop/surface.h>
#include <rsd_desktop/window.h>

/*
 * pcmanfm, which is LXDE's file manager.
 *
 * Its LXDE profile (lxde-common, /etc/xdg/pcmanfm/LXDE/pcmanfm.conf)
 * gives the defaults this follows:
 *
 *     win_width=640  win_height=480
 *     view_mode=icon
 *     show_hidden=0
 *     sort=name;ascending;
 *
 * and its window is a menu bar, a toolbar of Back/Forward/Up/Home with a
 * location bar after them, a side pane of Places, the view, and a
 * two-field status bar.
 *
 * THERE IS A FILESYSTEM UNDER IT.  Navigating really moves, the status
 * bar counts what is actually there, and a folder's size is the sum of
 * what is in it - not a number chosen to look plausible.  A file manager
 * drawn over a fixed list of names is a picture of a file manager.
 */

#define RSD_FILES_MAX_NODES 64U
#define RSD_FILES_MAX_CHILDREN 16U
#define RSD_FILES_NAME_BYTES 32U
#define RSD_FILES_PATH_BYTES 96U
#define RSD_FILES_MAX_SELECTED 16U

enum rsd_files_view {
    RSD_FILES_ICONS = 0,
    RSD_FILES_LIST
};

/* A node is a folder or a file; a folder's `size` is computed, never
 * stored, so it cannot drift from what is in it. */
struct rsd_files_node {
    char name[RSD_FILES_NAME_BYTES];
    bool folder;
    uint32_t bytes;             /* files only */
    uint32_t parent;            /* index, or RSD_FILES_MAX_NODES for / */
};

void rsd_files_reset(void);
/* Returns the new node's index, or RSD_FILES_MAX_NODES if it would not
 * fit or the parent is not a folder. */
uint32_t rsd_files_add(uint32_t parent, const char *name, bool folder,
    uint32_t bytes);
uint32_t rsd_files_root(void);

bool rsd_files_open(uint32_t folder);
uint32_t rsd_files_here(void);
bool rsd_files_up(void);
bool rsd_files_back(void);

uint32_t rsd_files_child_count(uint32_t folder);
uint32_t rsd_files_child(uint32_t folder, uint32_t at);
/* Every byte under a folder, counted rather than stored. */
uint32_t rsd_files_folder_bytes(uint32_t folder);
void rsd_files_path(uint32_t node, char *out, uint32_t capacity);

void rsd_files_select(uint32_t node, bool add);
void rsd_files_select_all(void);
void rsd_files_clear_selection(void);
bool rsd_files_is_selected(uint32_t node);
uint32_t rsd_files_selected_count(void);

/*
 * MOVING A THING BY DRAGGING IT.  Returns false and moves nothing if the
 * target is not a folder, or is the file's own folder, or is the thing
 * being dragged - or is inside it, which is the case that silently
 * detaches a whole subtree from the filesystem.
 */
/*
 * CREATE, RENAME AND DELETE.
 *
 * pcmanfm's context menu offers these and they have a filesystem under
 * them here, so they do the thing.  Each refuses rather than corrupting:
 * a name already taken, an empty name, a name with a slash in it (which
 * would be a path, not a name), and deleting the folder you are looking
 * at - which would leave the window showing a folder that is gone.
 */
/*
 * THE CLIPBOARD.
 *
 * Copy and Cut take the selection; Paste puts it in the folder being
 * shown.  A COPY duplicates the subtree - a copied folder that shared its
 * children with the original would be two names for one thing, and
 * deleting either would empty both.  A cut is spent once pasted; a copy
 * is not, which is what lets you paste the same thing twice.
 *
 * A name already in the target becomes "x (copy)", then "x (copy 2)",
 * rather than overwriting: pcmanfm's shape, and never a silent loss.
 */
bool rsd_files_copy_selection(bool cut);
bool rsd_files_clipboard_has(void);
bool rsd_files_clipboard_is_cut(void);
uint32_t rsd_files_paste_into(uint32_t folder);

bool rsd_files_rename(uint32_t node, const char *name);
bool rsd_files_remove(uint32_t node);
bool rsd_files_name_free(uint32_t folder, const char *name);

bool rsd_files_move(uint32_t node, uint32_t into);
bool rsd_files_is_inside(uint32_t node, uint32_t maybe_ancestor);

void rsd_files_set_view(enum rsd_files_view view);
enum rsd_files_view rsd_files_view_mode(void);

/* Where an entry sits, so a press can be turned into a node. */
/*
 * pcmanfm's show_hidden=0.  A name beginning with a dot is not shown.
 *
 * THE VIEWS USE THE VISIBLE LIST; THE TREE USES THE REAL ONE.  Paste,
 * remove, move and the name-collision check all keep counting every
 * child, because a hidden file is hidden, not absent - if they used the
 * visible list, pasting "README.txt" into a folder that already held a
 * hidden one would silently overwrite it.  Only what is drawn, and what
 * a click on what is drawn resolves to, goes through these two.
 */
void rsd_files_set_show_hidden(bool show);
bool rsd_files_show_hidden(void);
bool rsd_files_is_hidden(uint32_t node);
uint32_t rsd_files_visible_count(uint32_t folder);
uint32_t rsd_files_visible_child(uint32_t folder, uint32_t at);

/*
 * pcmanfm's single_click=0.  Off, a folder opens on the second click of a
 * double; on, the first click opens it.  The shell asks before deciding
 * what a press means, which is why this lives here rather than there.
 */
void rsd_files_set_single_click(bool single);
bool rsd_files_single_click(void);

bool rsd_files_entry_bounds(const struct rsd_window *window,
    uint32_t at, struct rsd_rect *out);

/* What a node IS, for anything drawing it outside this window - the
 * desktop draws ~/Desktop, and it should not need its own copy of the
 * extension-to-mark table to do it. */
const char *rsd_files_node_name(uint32_t node);
const char *rsd_files_node_mark(uint32_t node);
void rsd_files_draw_icon_at(struct rsd_surface *surface,
    struct rsd_rect clip, const char *mark, uint32_t size,
    uint32_t left, uint32_t top);

void rsd_files_draw(struct rsd_surface *surface,
    const struct rsd_window *window);

bool rsd_files_self_test(void);

#endif /* RSD_FILES_H */
