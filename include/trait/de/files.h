/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_DE_FILES_H
#define TRAIT_DE_FILES_H

#include <stdbool.h>
#include <stdint.h>

#include <trait/de/surface.h>
#include <trait/de/window.h>

/*
 * pcmanfm, the Trait OS file manager.
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

#define TRAIT_FILES_MAX_NODES 64U
#define TRAIT_FILES_MAX_CHILDREN 16U
#define TRAIT_FILES_NAME_BYTES 32U
#define TRAIT_FILES_PATH_BYTES 96U
#define TRAIT_FILES_MAX_SELECTED 16U

enum trait_files_view {
    TRAIT_FILES_ICONS = 0,
    TRAIT_FILES_LIST
};

/* A node is a folder or a file; a folder's `size` is computed, never
 * stored, so it cannot drift from what is in it. */
struct trait_files_node {
    char name[TRAIT_FILES_NAME_BYTES];
    bool folder;
    uint32_t bytes;             /* files only */
    uint32_t parent;            /* index, or TRAIT_FILES_MAX_NODES for / */
};

void trait_files_reset(void);
/* Returns the new node's index, or TRAIT_FILES_MAX_NODES if it would not
 * fit or the parent is not a folder. */
uint32_t trait_files_add(uint32_t parent, const char *name, bool folder,
    uint32_t bytes);
uint32_t trait_files_root(void);

bool trait_files_open(uint32_t folder);
uint32_t trait_files_here(void);
bool trait_files_up(void);
bool trait_files_back(void);

uint32_t trait_files_child_count(uint32_t folder);
uint32_t trait_files_child(uint32_t folder, uint32_t at);
/* Every byte under a folder, counted rather than stored. */
uint32_t trait_files_folder_bytes(uint32_t folder);
void trait_files_path(uint32_t node, char *out, uint32_t capacity);

void trait_files_select(uint32_t node, bool add);
void trait_files_select_all(void);
void trait_files_clear_selection(void);
bool trait_files_is_selected(uint32_t node);
uint32_t trait_files_selected_count(void);

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
bool trait_files_copy_selection(bool cut);
bool trait_files_clipboard_has(void);
bool trait_files_clipboard_is_cut(void);
uint32_t trait_files_paste_into(uint32_t folder);

bool trait_files_rename(uint32_t node, const char *name);
bool trait_files_remove(uint32_t node);
bool trait_files_name_free(uint32_t folder, const char *name);

bool trait_files_move(uint32_t node, uint32_t into);
bool trait_files_is_inside(uint32_t node, uint32_t maybe_ancestor);

void trait_files_set_view(enum trait_files_view view);
enum trait_files_view trait_files_view_mode(void);

/* Where an entry sits, so a press can be turned into a node. */
bool trait_files_entry_bounds(const struct trait_window *window,
    uint32_t at, struct trait_rect *out);

/* What a node IS, for anything drawing it outside this window - the
 * desktop draws ~/Desktop, and it should not need its own copy of the
 * extension-to-mark table to do it. */
const char *trait_files_node_name(uint32_t node);
const char *trait_files_node_mark(uint32_t node);
void trait_files_draw_icon_at(struct trait_surface *surface,
    struct trait_rect clip, const char *mark, uint32_t size,
    uint32_t left, uint32_t top);

void trait_files_draw(struct trait_surface *surface,
    const struct trait_window *window);

bool trait_files_self_test(void);

#endif /* TRAIT_DE_FILES_H */
