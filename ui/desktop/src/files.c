/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd_desktop/files.h>

#include <rsd_desktop/oxygen.h>

#include "rsd_icons.h"

#include <rsd_desktop/font.h>
#include <rsd_desktop/theme.h>

#include "rsd_files_art.h"

/* ================================================================ METRICS
 *
 * pcmanfm's own, from its LXDE profile and its window.
 */
/*
 * WHAT IS GONE, AND WHY IT WAS ALWAYS GOING TO GO.
 *
 * pcmanfm has a menu bar, a places pane and sortable column headings,
 * and this had all three because it is a copy of pcmanfm.  None of them
 * was ever hit-tested: File opened nothing, Desktop in the places pane
 * went nowhere, and pressing Name sorted by nothing.  Fourteen controls
 * that were pictures of controls, which is the one thing this desktop
 * does not draw.  The path was worse than dead - it was drawn as a
 * sunken entry, which is a window telling you it will take what you
 * type.  It is a line of text now, because that is what it is.
 */
/*
 * THE CHROME, in the order Nautilus stacks it.
 *
 * A menu bar, a toolbar of icons, a row of path BUTTONS - not an
 * editable text field, which is what this had - and then the body,
 * which is a Places list down the side and the view beside it. A
 * status line along the bottom.
 *
 * The window had a path written across the top as text and nothing
 * else. That is a file list with a caption; the thing being copied is
 * a file manager, and almost all of what makes one is the furniture
 * round the list.
 */
#define FILES_MENU 18U              /* File Edit View Bookmarks Help */
#define FILES_TOOL 32U              /* the icon toolbar */
#define FILES_PATH 26U              /* the breadcrumb buttons */
#define FILES_SIDE 140U             /* the Places list */
#define FILES_CHROME (FILES_MENU + FILES_TOOL + FILES_PATH)
#define FILES_STATUS 18U
#define FILES_PAD 6U

#define FILES_CELL_WIDTH 96U        /* the icon view's grid */
#define FILES_CELL_HEIGHT 40U
/* ONE SIZE, AND IT IS SIXTEEN.  The icons are gentoo's 16x15 and there
 * is no bigger version of them; blowing one up to 48 would be a picture
 * of an icon.  So the icon view draws the same mark the list does and
 * gives it a wider cell, which is the brief view every file manager of
 * that decade had. */
#define FILES_ICON 16U
#define FILES_SMALL 16U

#define FILES_ROW 18U               /* the detailed list */

/* ================================================================== STATE */

static struct rsd_files_node nodes[RSD_FILES_MAX_NODES];
static uint32_t node_count;
static uint32_t children[RSD_FILES_MAX_NODES][RSD_FILES_MAX_CHILDREN];
static uint32_t child_counts[RSD_FILES_MAX_NODES];

static uint32_t here;
static uint32_t history[16];
static uint32_t history_depth;

static uint32_t selected[RSD_FILES_MAX_SELECTED];
static uint32_t selected_count;
/* The list, because that is what gentoo was and what the icons were
 * drawn for: a row high, a name beside it. */
static enum rsd_files_view view_mode = RSD_FILES_LIST;
static bool show_hidden;           /* pcmanfm: show_hidden=0 */
static bool single_click;          /* pcmanfm: single_click=0 */

static uint32_t clipboard[RSD_FILES_MAX_SELECTED];
static uint32_t clip_count;
static bool clip_cut;

/* ================================================================ HELPERS */

static void copy(char *out, const char *text, uint32_t capacity)
{
    uint32_t at = 0U;

    while (text != NULL && text[at] != '\0' && at + 1U < capacity) {
        out[at] = text[at];
        ++at;
    }
    out[at] = '\0';
}

static bool same(const char *a, const char *b)
{
    uint32_t at = 0U;

    while (a[at] != '\0' && b[at] != '\0') {
        if (a[at] != b[at]) {
            return false;
        }
        ++at;
    }
    return a[at] == b[at];
}

static bool ends_with(const char *name, const char *tail)
{
    uint32_t n = 0U;
    uint32_t t = 0U;

    while (name[n] != '\0') {
        ++n;
    }
    while (tail[t] != '\0') {
        ++t;
    }
    if (t > n) {
        return false;
    }
    return same(name + (n - t), tail);
}

/* Which mark a name gets, by extension, the way a file manager does it -
 * and the generic sheet when nothing matches, rather than guessing. */
static const char *mark_for(const struct rsd_files_node *node)
{
    if (node->folder) {
        return "folder";
    }
    if (ends_with(node->name, ".txt") || ends_with(node->name, ".conf")) {
        return "text-x-generic";
    }
    if (ends_with(node->name, ".png") || ends_with(node->name, ".jpg")) {
        return "image-x-generic";
    }
    if (ends_with(node->name, ".ogg") || ends_with(node->name, ".wav")) {
        return "audio-x-generic";
    }
    if (ends_with(node->name, ".sh") || ends_with(node->name, ".bin")) {
        return "application-x-executable";
    }
    return "text-x-generic";
}

static const struct rsd_files_art_entry *art_named(const char *name)
{
    uint32_t at;
    uint32_t index;

    for (index = 0U; index < RSD_FILES_ART_COUNT; ++index) {
        const char *candidate = rsd_files_art[index].name;

        for (at = 0U; ; ++at) {
            if (candidate[at] != name[at]) {
                break;
            }
            if (candidate[at] == '\0') {
                return &rsd_files_art[index];
            }
        }
    }
    return NULL;
}

static uint32_t art_plane(uint32_t size)
{
    uint32_t at;

    for (at = 0U; at < RSD_FILES_ART_SIZES; ++at) {
        if (rsd_files_art_size[at] == size) {
            return at;
        }
    }
    return RSD_FILES_ART_SIZES;
}

static void draw_icon(struct rsd_surface *surface, struct rsd_rect clip,
    const char *name, uint32_t size, uint32_t left, uint32_t top)
{
    const struct rsd_files_art_entry *art = art_named(name);
    uint32_t plane = art_plane(size);
    uint32_t x;
    uint32_t y;

    if (art == NULL || plane >= RSD_FILES_ART_SIZES) {
        return;
    }
    for (y = 0U; y < size; ++y) {
        for (x = 0U; x < size; ++x) {
            uint32_t at = y * size + x;
            uint32_t alpha = art->alpha[plane][at];
            uint32_t under;

            if (alpha == 0U) {
                continue;
            }
            under = rsd_surface_read(surface, left + x, top + y);
            rsd_surface_plot(surface, clip, left + x, top + y,
                rsd_blend(under, art->pixels[plane][at], alpha));
        }
    }
}

static uint32_t number(char *out, uint32_t value, uint32_t capacity)
{
    char digits[12];
    uint32_t length = 0U;
    uint32_t at = 0U;

    if (value == 0U) {
        digits[length++] = '0';
    }
    while (value != 0U && length < sizeof(digits)) {
        digits[length++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (length != 0U && at + 1U < capacity) {
        out[at++] = digits[--length];
    }
    out[at] = '\0';
    return at;
}

static void append(char *out, const char *text, uint32_t capacity)
{
    uint32_t at = 0U;
    uint32_t from = 0U;

    while (out[at] != '\0') {
        ++at;
    }
    while (text[from] != '\0' && at + 1U < capacity) {
        out[at++] = text[from++];
    }
    out[at] = '\0';
}

/* Bytes, in the unit that does not overstate the precision: whole KiB and
 * one decimal of MiB, which is what a file manager shows. */
static void human(char *out, uint32_t bytes, uint32_t capacity)
{
    if (bytes >= 1048576U) {
        uint32_t tenths = bytes / 104858U;

        (void)number(out, tenths / 10U, capacity);
        append(out, ".", capacity);
        {
            char digit[2];

            digit[0] = (char)('0' + (tenths % 10U));
            digit[1] = '\0';
            append(out, digit, capacity);
        }
        append(out, " MiB", capacity);
        return;
    }
    if (bytes >= 1024U) {
        (void)number(out, bytes / 1024U, capacity);
        append(out, " KiB", capacity);
        return;
    }
    (void)number(out, bytes, capacity);
    append(out, " bytes", capacity);
}

/* ================================================================== MODEL */

const char *rsd_files_node_name(uint32_t node)
{
    if (node >= node_count) {
        return "";
    }
    return nodes[node].name;
}

const char *rsd_files_node_mark(uint32_t node)
{
    if (node >= node_count) {
        return "text-x-generic";
    }
    return mark_for(&nodes[node]);
}

void rsd_files_draw_icon_at(struct rsd_surface *surface,
    struct rsd_rect clip, const char *mark, uint32_t size,
    uint32_t left, uint32_t top)
{
    draw_icon(surface, clip, mark, size, left, top);
}

void rsd_files_reset(void)
{
    /* Defaults, for the same reason the shell restores its own: a check
     * that toggled a setting must not hand it to the next one. */
    show_hidden = false;
    single_click = false;
    uint32_t at;

    clip_count = 0U;
    clip_cut = false;
    node_count = 0U;
    for (at = 0U; at < RSD_FILES_MAX_NODES; ++at) {
        child_counts[at] = 0U;
    }
    selected_count = 0U;
    history_depth = 0U;
    copy(nodes[0].name, "/", RSD_FILES_NAME_BYTES);
    nodes[0].folder = true;
    nodes[0].bytes = 0U;
    nodes[0].parent = RSD_FILES_MAX_NODES;
    node_count = 1U;
    here = 0U;
}

uint32_t rsd_files_root(void)
{
    return 0U;
}

uint32_t rsd_files_add(uint32_t parent, const char *name, bool folder,
    uint32_t bytes)
{
    uint32_t index;

    if (node_count >= RSD_FILES_MAX_NODES || name == NULL ||
            parent >= node_count || !nodes[parent].folder) {
        return RSD_FILES_MAX_NODES;
    }
    if (child_counts[parent] >= RSD_FILES_MAX_CHILDREN) {
        return RSD_FILES_MAX_NODES;
    }
    index = node_count++;
    copy(nodes[index].name, name, RSD_FILES_NAME_BYTES);
    nodes[index].folder = folder;
    nodes[index].bytes = folder ? 0U : bytes;
    nodes[index].parent = parent;
    children[parent][child_counts[parent]++] = index;
    return index;
}

uint32_t rsd_files_child_count(uint32_t folder)
{
    if (folder >= node_count) {
        return 0U;
    }
    return child_counts[folder];
}

uint32_t rsd_files_child(uint32_t folder, uint32_t at)
{
    if (folder >= node_count || at >= child_counts[folder]) {
        return RSD_FILES_MAX_NODES;
    }
    return children[folder][at];
}

/*
 * COUNTED, NOT STORED.  A folder's size is whatever is under it right
 * now, so it cannot drift from the truth the way a cached number does -
 * and the status bar can stand behind what it prints.
 */
uint32_t rsd_files_folder_bytes(uint32_t folder)
{
    uint32_t total = 0U;
    uint32_t at;

    if (folder >= node_count) {
        return 0U;
    }
    for (at = 0U; at < child_counts[folder]; ++at) {
        uint32_t child = children[folder][at];

        total += nodes[child].folder ?
            rsd_files_folder_bytes(child) : nodes[child].bytes;
    }
    return total;
}

void rsd_files_path(uint32_t node, char *out, uint32_t capacity)
{
    uint32_t chain[12];
    uint32_t depth = 0U;
    uint32_t walk = node;

    if (out == NULL || capacity == 0U) {
        return;
    }
    out[0] = '\0';
    if (node >= node_count) {
        return;
    }
    while (walk != 0U && depth < 12U) {
        chain[depth++] = walk;
        walk = nodes[walk].parent;
    }
    if (depth == 0U) {
        append(out, "/", capacity);
        return;
    }
    while (depth != 0U) {
        append(out, "/", capacity);
        append(out, nodes[chain[--depth]].name, capacity);
    }
}

bool rsd_files_open(uint32_t folder)
{
    if (folder >= node_count || !nodes[folder].folder) {
        return false;
    }
    if (history_depth < 16U) {
        history[history_depth++] = here;
    }
    here = folder;
    selected_count = 0U;
    return true;
}

uint32_t rsd_files_here(void)
{
    return here;
}

bool rsd_files_up(void)
{
    if (nodes[here].parent >= node_count) {
        return false;
    }
    return rsd_files_open(nodes[here].parent);
}

bool rsd_files_back(void)
{
    if (history_depth == 0U) {
        return false;
    }
    /* Going back must NOT push where you were onto the history, or Back
     * becomes a switch between two folders forever. */
    here = history[--history_depth];
    selected_count = 0U;
    return true;
}

/* ============================================================== SELECTION */

bool rsd_files_is_selected(uint32_t node)
{
    uint32_t at;

    for (at = 0U; at < selected_count; ++at) {
        if (selected[at] == node) {
            return true;
        }
    }
    return false;
}

void rsd_files_select(uint32_t node, bool add)
{
    uint32_t at;

    if (node >= node_count) {
        return;
    }
    if (!add) {
        selected[0] = node;
        selected_count = 1U;
        return;
    }
    if (rsd_files_is_selected(node)) {
        for (at = 0U; at < selected_count; ++at) {
            if (selected[at] == node) {
                break;
            }
        }
        for (; at + 1U < selected_count; ++at) {
            selected[at] = selected[at + 1U];
        }
        --selected_count;
        return;
    }
    if (selected_count < RSD_FILES_MAX_SELECTED) {
        selected[selected_count++] = node;
    }
}

void rsd_files_select_all(void)
{
    uint32_t at;

    selected_count = 0U;
    for (at = 0U; at < child_counts[here] &&
            selected_count < RSD_FILES_MAX_SELECTED; ++at) {
        selected[selected_count++] = children[here][at];
    }
}

void rsd_files_clear_selection(void)
{
    selected_count = 0U;
}

uint32_t rsd_files_selected_count(void)
{
    return selected_count;
}

/* A name is a NAME, not a path: a slash in it would make the tree a lie
 * about where things are.  Empty is refused too - a file with no name is
 * a row you cannot click on. */
static bool name_is_legal(const char *name)
{
    uint32_t at = 0U;

    if (name == NULL || name[0] == '\0') {
        return false;
    }
    while (name[at] != '\0') {
        if (name[at] == '/') {
            return false;
        }
        ++at;
    }
    return at + 1U < RSD_FILES_NAME_BYTES;
}

bool rsd_files_name_free(uint32_t folder, const char *name)
{
    uint32_t at;

    if (folder >= node_count || !name_is_legal(name)) {
        return false;
    }
    for (at = 0U; at < child_counts[folder]; ++at) {
        if (same(nodes[children[folder][at]].name, name)) {
            return false;
        }
    }
    return true;
}

bool rsd_files_copy_selection(bool cut)
{
    uint32_t at;

    if (selected_count == 0U) {
        return false;
    }
    for (at = 0U; at < selected_count; ++at) {
        clipboard[at] = selected[at];
    }
    clip_count = selected_count;
    clip_cut = cut;
    return true;
}

bool rsd_files_clipboard_has(void)
{
    return clip_count != 0U;
}

bool rsd_files_clipboard_is_cut(void)
{
    return clip_cut;
}

/*
 * "x.txt" already there becomes "x (copy).txt", then "x (copy 2).txt".
 * The suffix goes before the EXTENSION, because "x.txt (copy)" is a file
 * the desktop no longer knows how to open.
 */
static void unique_name(uint32_t folder, const char *name, char *out)
{
    uint32_t dot = 0U;
    uint32_t at = 0U;
    uint32_t nth = 1U;

    copy(out, name, RSD_FILES_NAME_BYTES);
    if (rsd_files_name_free(folder, out)) {
        return;
    }
    while (name[at] != '\0') {
        if (name[at] == '.' && at != 0U) {
            dot = at;
        }
        ++at;
    }
    if (dot == 0U) {
        dot = at;
    }
    while (nth < 100U) {
        uint32_t put = 0U;
        uint32_t from;

        for (from = 0U; from < dot && put + 1U < RSD_FILES_NAME_BYTES;
                ++from) {
            out[put++] = name[from];
        }
        {
            static const char TAG[] = " (copy";
            uint32_t tag = 0U;

            while (TAG[tag] != '\0' &&
                    put + 1U < RSD_FILES_NAME_BYTES) {
                out[put++] = TAG[tag++];
            }
        }
        if (nth > 1U && put + 3U < RSD_FILES_NAME_BYTES) {
            out[put++] = ' ';
            out[put++] = (char)('0' + nth);
        }
        if (put + 1U < RSD_FILES_NAME_BYTES) {
            out[put++] = ')';
        }
        for (from = dot; name[from] != '\0' &&
                put + 1U < RSD_FILES_NAME_BYTES; ++from) {
            out[put++] = name[from];
        }
        out[put] = '\0';
        if (rsd_files_name_free(folder, out)) {
            return;
        }
        ++nth;
    }
}

/* A DEEP copy: the children come too, as new nodes.  Sharing them would
 * make two names for one thing, and deleting either would empty both. */
static uint32_t clone_into(uint32_t node, uint32_t folder,
    const char *as_name)
{
    uint32_t made = rsd_files_add(folder, as_name, nodes[node].folder,
                                    nodes[node].bytes);
    uint32_t at;

    if (made >= RSD_FILES_MAX_NODES) {
        return RSD_FILES_MAX_NODES;
    }
    for (at = 0U; at < child_counts[node]; ++at) {
        uint32_t child = children[node][at];

        if (clone_into(child, made, nodes[child].name) >=
                RSD_FILES_MAX_NODES) {
            return RSD_FILES_MAX_NODES;
        }
    }
    return made;
}

uint32_t rsd_files_paste_into(uint32_t folder)
{
    char name[RSD_FILES_NAME_BYTES];
    uint32_t done = 0U;
    uint32_t at;

    if (clip_count == 0U || folder >= node_count ||
            !nodes[folder].folder) {
        return 0U;
    }
    for (at = 0U; at < clip_count; ++at) {
        uint32_t node = clipboard[at];

        if (node >= node_count) {
            continue;
        }
        /* The same refusals a drag has, for the same reasons. */
        if (node == folder || rsd_files_is_inside(folder, node)) {
            continue;
        }
        if (clip_cut) {
            if (nodes[node].parent == folder) {
                continue;
            }
            if (rsd_files_move(node, folder)) {
                ++done;
            }
            continue;
        }
        unique_name(folder, nodes[node].name, name);
        if (clone_into(node, folder, name) < RSD_FILES_MAX_NODES) {
            ++done;
        }
    }
    if (clip_cut) {
        /* A cut is SPENT once pasted; a copy is not, so the same thing
         * can be pasted twice. */
        clip_count = 0U;
        clip_cut = false;
    }
    selected_count = 0U;
    return done;
}

bool rsd_files_rename(uint32_t node, const char *name)
{
    if (node == 0U || node >= node_count) {
        return false;
    }
    /* Renaming to what it is already called is not a failure and not a
     * change - saying yes to it would put a no-op in the undo history a
     * file manager does not have. */
    if (same(nodes[node].name, name)) {
        return false;
    }
    if (!rsd_files_name_free(nodes[node].parent, name)) {
        return false;
    }
    copy(nodes[node].name, name, RSD_FILES_NAME_BYTES);
    return true;
}

/*
 * Removing takes the SUBTREE with it.  Leaving the children behind would
 * leave nodes nobody can reach, and the count of what is in the parent
 * would still be right while the filesystem underneath quietly filled up.
 */
static void detach(uint32_t folder, uint32_t node)
{
    uint32_t at;

    for (at = 0U; at < child_counts[folder]; ++at) {
        if (children[folder][at] != node) {
            continue;
        }
        for (; at + 1U < child_counts[folder]; ++at) {
            children[folder][at] = children[folder][at + 1U];
        }
        --child_counts[folder];
        return;
    }
}

static void wipe(uint32_t node)
{
    while (child_counts[node] != 0U) {
        uint32_t child = children[node][child_counts[node] - 1U];

        wipe(child);
        --child_counts[node];
    }
    nodes[node].name[0] = '\0';
    nodes[node].bytes = 0U;
    nodes[node].parent = RSD_FILES_MAX_NODES;
}

bool rsd_files_remove(uint32_t node)
{
    uint32_t parent;

    if (node == 0U || node >= node_count) {
        return false;
    }
    /* Not the folder you are looking at, and not one you are inside:
     * either leaves the window showing something that is gone. */
    if (rsd_files_is_inside(here, node)) {
        return false;
    }
    parent = nodes[node].parent;
    if (parent >= node_count) {
        return false;
    }
    detach(parent, node);
    wipe(node);
    selected_count = 0U;
    return true;
}

bool rsd_files_is_inside(uint32_t node, uint32_t maybe_ancestor)
{
    uint32_t walk;

    if (node >= node_count || maybe_ancestor >= node_count) {
        return false;
    }
    walk = node;
    while (walk != 0U) {
        if (walk == maybe_ancestor) {
            return true;
        }
        walk = nodes[walk].parent;
    }
    return false;
}

bool rsd_files_move(uint32_t node, uint32_t into)
{
    uint32_t from;
    uint32_t at;

    if (node >= node_count || into >= node_count || node == 0U) {
        return false;
    }
    if (!nodes[into].folder) {
        return false;
    }
    if (node == into) {
        return false;
    }
    /*
     * A FOLDER CANNOT BE MOVED INSIDE ITSELF.  Allowing it detaches the
     * whole subtree from the root - every node still exists, nothing can
     * reach any of them, and the bug shows up later as a folder that
     * vanished rather than as a bad drag.
     */
    if (rsd_files_is_inside(into, node)) {
        return false;
    }
    from = nodes[node].parent;
    if (from == into) {
        return false;
    }
    if (from >= node_count ||
            child_counts[into] >= RSD_FILES_MAX_CHILDREN) {
        return false;
    }
    for (at = 0U; at < child_counts[from]; ++at) {
        if (children[from][at] != node) {
            continue;
        }
        for (; at + 1U < child_counts[from]; ++at) {
            children[from][at] = children[from][at + 1U];
        }
        --child_counts[from];
        break;
    }
    children[into][child_counts[into]++] = node;
    nodes[node].parent = into;
    selected_count = 0U;
    return true;
}

void rsd_files_set_view(enum rsd_files_view view)
{
    view_mode = view;
}

enum rsd_files_view rsd_files_view_mode(void)
{
    return view_mode;
}

/* ================================================================= LAYOUT */

static struct rsd_rect view_area(const struct rsd_window *window)
{
    struct rsd_rect client = rsd_window_client(window);
    struct rsd_rect box;

    box.x = client.x;
    box.y = client.y + FILES_MENU + FILES_TOOL + FILES_PATH;
    box.width = client.width;
    box.height = client.height > FILES_CHROME + FILES_STATUS ?
        client.height - FILES_CHROME - FILES_STATUS : 0U;
    /* The Places list takes its width off the left of the view, so a
     * hit test done against this rect lands where the drawing did. Two
     * definitions of where the view is means clicking a file selects
     * the one above it. */
    if (box.width > FILES_SIDE) {
        box.x += FILES_SIDE;
        box.width -= FILES_SIDE;
    }
    return box;
}

void rsd_files_set_show_hidden(bool show)
{
    show_hidden = show;
}

bool rsd_files_show_hidden(void)
{
    return show_hidden;
}

bool rsd_files_is_hidden(uint32_t node)
{
    if (node >= node_count) {
        return false;
    }
    return nodes[node].name[0] == '.';
}

uint32_t rsd_files_visible_count(uint32_t folder)
{
    uint32_t at;
    uint32_t seen = 0U;

    if (folder >= node_count) {
        return 0U;
    }
    if (show_hidden) {
        return child_counts[folder];
    }
    for (at = 0U; at < child_counts[folder]; ++at) {
        if (!rsd_files_is_hidden(children[folder][at])) {
            ++seen;
        }
    }
    return seen;
}

uint32_t rsd_files_visible_child(uint32_t folder, uint32_t at)
{
    uint32_t scan;
    uint32_t seen = 0U;

    if (folder >= node_count) {
        return RSD_FILES_MAX_NODES;
    }
    if (show_hidden) {
        return rsd_files_child(folder, at);
    }
    for (scan = 0U; scan < child_counts[folder]; ++scan) {
        uint32_t node = children[folder][scan];

        if (rsd_files_is_hidden(node)) {
            continue;
        }
        if (seen == at) {
            return node;
        }
        ++seen;
    }
    return RSD_FILES_MAX_NODES;
}

void rsd_files_set_single_click(bool single)
{
    single_click = single;
}

bool rsd_files_single_click(void)
{
    return single_click;
}

bool rsd_files_entry_bounds(const struct rsd_window *window,
    uint32_t at, struct rsd_rect *out)
{
    struct rsd_rect box = view_area(window);
    uint32_t columns;

    if (window == NULL || out == NULL ||
            at >= rsd_files_visible_count(here)) {
        return false;
    }
    if (view_mode == RSD_FILES_LIST) {
        /* No header row to skip any more: the first entry is at the
         * top of the view. */
        out->x = box.x;
        out->y = box.y + at * FILES_ROW;
        out->width = box.width;
        out->height = FILES_ROW;
        return true;
    }
    columns = box.width / FILES_CELL_WIDTH;
    if (columns == 0U) {
        columns = 1U;
    }
    out->x = box.x + FILES_PAD + (at % columns) * FILES_CELL_WIDTH;
    out->y = box.y + FILES_PAD + (at / columns) * FILES_CELL_HEIGHT;
    out->width = FILES_CELL_WIDTH;
    out->height = FILES_CELL_HEIGHT;
    return true;
}


/*
 * THE CHROME, WHICH IS NOT A STRIP OF GREY.
 *
 * Every band across this window - the menu bar, the toolbar, the path,
 * the sidebar, the status line - used to be rsd_surface_fill() with
 * RSD_BG and a hard line ruled under it. Five flat grey bands with
 * four black rules between them, which is a GTK 2 window and reads as
 * one.
 *
 * Oxygen has none of those lines and none of those fills. The whole
 * window is one lit sheet: a band takes its colour from the window's own
 * gradient sampled at that band's absolute y, and the only line anywhere
 * is the one where the VIEW is sunk into the sheet. That is what makes
 * the chrome read as one surface with words on it rather than as a stack
 * of boxes.
 *
 * Which is why this needs the window and not just the client rect - the
 * gradient belongs to the window, and a band that sampled it from its
 * own top would restart it and put a second highlight halfway down.
 */
static void chrome(struct rsd_surface *surface, struct rsd_rect clip,
    struct rsd_rect strip, const struct rsd_window *window)
{
    uint32_t x;
    uint32_t y;

    for (y = strip.y; y < strip.y + strip.height; ++y) {
        uint32_t row = rsd_oxy_window(RSD_BG, y, window->frame.y,
                                        window->frame.height);

        for (x = strip.x; x < strip.x + strip.width; ++x) {
            rsd_surface_plot(surface, clip, x, y,
                rsd_oxy_pool(RSD_BG, row, x, y, window->frame));
        }
    }
}

/* Where a line of text sits inside a band, so nothing here has to know
 * which face is selected. */
static uint32_t baseline(struct rsd_rect strip)
{
    return strip.y + (strip.height + rsd_font_ascent()) / 2U - 1U;
}

static void draw_status(struct rsd_surface *surface,
    struct rsd_rect client, const struct rsd_window *window)
{
    struct rsd_rect strip;
    char left[48];
    char right[48];
    uint32_t width;

    strip.x = client.x;
    strip.y = client.y + client.height - FILES_STATUS;
    strip.width = client.width;
    strip.height = FILES_STATUS;
    chrome(surface, client, strip, window);

    left[0] = '\0';
    if (selected_count > 1U) {
        uint32_t bytes = 0U;
        uint32_t at;

        for (at = 0U; at < selected_count; ++at) {
            bytes += nodes[selected[at]].folder ?
                rsd_files_folder_bytes(selected[at]) :
                nodes[selected[at]].bytes;
        }
        (void)number(left, selected_count, sizeof(left));
        append(left, " items selected (", sizeof(left));
        {
            char size[24];

            human(size, bytes, sizeof(size));
            append(left, size, sizeof(left));
        }
        append(left, ")", sizeof(left));
    } else if (selected_count == 1U) {
        char size[24];

        append(left, "\"", sizeof(left));
        append(left, nodes[selected[0]].name, sizeof(left));
        append(left, "\" (", sizeof(left));
        human(size, nodes[selected[0]].folder ?
              rsd_files_folder_bytes(selected[0]) :
              nodes[selected[0]].bytes, sizeof(size));
        append(left, size, sizeof(left));
        append(left, ") selected", sizeof(left));
    } else {
        (void)number(left, child_counts[here], sizeof(left));
        append(left, child_counts[here] == 1U ? " item" : " items",
               sizeof(left));
    }
    rsd_font_draw(surface, strip, strip.x + FILES_PAD,
                    strip.y + 14U, left, RSD_TEXT);

    /*
     * The right-hand field is what is in this folder, counted - and
     * just the figure, because the line above says which folder.
     * pcmanfm puts free space there; there is no filesystem under this
     * one to ask, and a status bar that makes up a figure is worse than
     * one that leaves the field out.
     */
    right[0] = '\0';
    human(right, rsd_files_folder_bytes(here), sizeof(right));
    width = rsd_font_width(right);
    if (strip.width > width + FILES_PAD) {
        rsd_font_draw(surface, strip,
            strip.x + strip.width - width - FILES_PAD, strip.y + 14U,
            right, RSD_TEXT);
    }
}

/*
 * WHAT IS SELECTED, and it is not a blue rectangle.
 *
 * Oxygen draws a selection as a rounded slab with the highlight colour
 * graded down it - lighter at the top, the highlight itself at the
 * bottom - which is the same light falling on the same glass as
 * everything else in the window. A flat fill with square corners is the
 * one thing in a view that says the year.
 */
static void selection(struct rsd_surface *surface,
    struct rsd_rect clip, struct rsd_rect cell)
{
    uint32_t light = rsd_oxy_mix(RSD_SEL_BG, 0xFFFFFFU, 70U);
    uint32_t x;
    uint32_t y;

    for (y = cell.y; y < cell.y + cell.height; ++y) {
        uint32_t row = rsd_oxy_mix(light, RSD_SEL_BG,
            (y - cell.y) * 255U / (cell.height ? cell.height : 1U));

        for (x = cell.x; x < cell.x + cell.width; ++x) {
            if (rsd_oxy_outside(cell, x, y, 3U, false)) {
                continue;
            }
            rsd_surface_plot(surface, clip, x, y, row);
        }
    }
}

static void draw_entries(struct rsd_surface *surface,
    const struct rsd_window *window, struct rsd_rect box)
{
    struct rsd_rect cell;
    uint32_t at;

    for (at = 0U; at < rsd_files_visible_count(here); ++at) {
        uint32_t node = rsd_files_visible_child(here, at);
        bool lit = rsd_files_is_selected(node);

        if (!rsd_files_entry_bounds(window, at, &cell)) {
            continue;
        }
        if (cell.y + cell.height > box.y + box.height) {
            break;
        }
        if (view_mode == RSD_FILES_LIST) {
            char size[24];

            if (lit) {
                selection(surface, box, cell);
            } else if ((at & 1U) != 0U) {
                rsd_surface_fill(surface, box, cell,
                                   RSD_BASE_PRELIGHT);
            }
            draw_icon(surface, box, mark_for(&nodes[node]), FILES_SMALL,
                      cell.x + 4U, cell.y + (cell.height > FILES_SMALL ?
                          (cell.height - FILES_SMALL) / 2U : 0U));
            rsd_font_draw(surface, box, cell.x + 24U, baseline(cell),
                nodes[node].name, lit ? RSD_SEL_FG : RSD_TEXT);
            /*
             * Name on the left, size on the right, and nothing in
             * between.  There was a Description column reading "Folder"
             * or "Plain text" against every row, next to an icon that
             * had already said so.
             *
             * A FOLDER HAS NO SIZE here, and an empty right-hand end
             * says so better than a nought does.
             */
            if (!nodes[node].folder) {
                uint32_t width;

                human(size, nodes[node].bytes, sizeof(size));
                width = rsd_font_width(size);
                if (cell.width > width + FILES_PAD) {
                    rsd_font_draw(surface, box,
                        cell.x + cell.width - width - FILES_PAD,
                        baseline(cell), size,
                        lit ? RSD_SEL_FG : RSD_TEXT);
                }
            }
            continue;
        }

        if (lit) {
            struct rsd_rect wash = cell;

            wash.x += 2U;
            wash.width -= 4U;
            selection(surface, box, wash);
        }
        draw_icon(surface, box, mark_for(&nodes[node]), FILES_ICON,
                  cell.x + (cell.width - FILES_ICON) / 2U, cell.y + 4U);
        {
            uint32_t width = rsd_font_width(nodes[node].name);
            uint32_t pen = cell.x + (cell.width > width ?
                (cell.width - width) / 2U : 0U);

            rsd_font_draw(surface, box, pen, cell.y + FILES_ICON + 18U,
                nodes[node].name, lit ? RSD_SEL_FG : RSD_FG);
        }
    }
}

/*
 * THE MENU BAR. Five words, in Nautilus's order.
 *
 * They do not open anything yet, and that is the one thing this file
 * will not pretend about: a word that looks like a menu and answers
 * nothing is a control drawn as something it is not. They are drawn in
 * the dim ink rather than the live one, which is what a disabled menu
 * looks like, so the window is honest about it until the menus exist.
 */
static void draw_menubar(struct rsd_surface *surface,
    struct rsd_rect client, const struct rsd_window *window)
{
    static const char *const WORDS[] = {
        "File", "Edit", "View", "Bookmarks", "Help"
    };
    struct rsd_rect strip = client;
    uint32_t pen = client.x + FILES_PAD;
    uint32_t at;

    strip.height = FILES_MENU;
    chrome(surface, client, strip, window);
    for (at = 0U; at < sizeof(WORDS) / sizeof(WORDS[0]); ++at) {
        rsd_font_draw(surface, strip, pen, baseline(strip), WORDS[at],
                        RSD_FRAME_INK_DIM);
        pen += rsd_font_width(WORDS[at]) + 14U;
    }
}

/*
 * THE TOOLBAR, and every button on it answers.
 *
 * Back, Forward and Up are the navigation Nautilus puts first; Reload
 * and Home follow; Search sits at the far end. Back and Forward grey
 * out when there is nowhere to go, because a button that is always lit
 * tells you nothing about where you are.
 *
 * The icons are Tango's, vendored byte for byte - see
 * assets/icons/SOURCE.txt.
 */
#define TOOL_STEP 30U

static void draw_toolbar(struct rsd_surface *surface,
    struct rsd_rect client, const struct rsd_window *window)
{
    static const uint32_t LEFT[] = {
        RSD_ICON_BACK, RSD_ICON_FORWARD, RSD_ICON_UP,
        RSD_ICON_RELOAD, RSD_ICON_HOME
    };
    struct rsd_rect strip = client;
    uint32_t top;
    uint32_t at;

    strip.y = client.y + FILES_MENU;
    strip.height = FILES_TOOL;
    chrome(surface, client, strip, window);
    top = strip.y + (FILES_TOOL - RSD_ICON_SIZE) / 2U;

    for (at = 0U; at < sizeof(LEFT) / sizeof(LEFT[0]); ++at) {
        rsd_oxy_blit(surface, strip, client.x + FILES_PAD + at * TOOL_STEP,
                       top, rsd_icons[LEFT[at]], RSD_ICON_SIZE);
    }
    if (client.width > TOOL_STEP + FILES_PAD * 2U) {
        rsd_oxy_blit(surface, strip,
            client.x + client.width - FILES_PAD - RSD_ICON_SIZE, top,
            rsd_icons[RSD_ICON_FIND], RSD_ICON_SIZE);
    }
}

/*
 * THE PATH, AS BUTTONS.
 *
 * This is the one piece of Nautilus that people remember and it is the
 * piece this window did not have: the folder you are in is not written
 * across the top as text, it is a row of buttons, one per component,
 * with the one you are in pressed. You can see the whole way back and
 * click any of it.
 *
 * The components are split out of the path rather than held separately,
 * so the buttons cannot disagree with where the window actually is.
 */
static void draw_breadcrumbs(struct rsd_surface *surface,
    struct rsd_rect client, const char *path,
    const struct rsd_window *window)
{
    struct rsd_rect strip = client;
    uint32_t pen;
    uint32_t at = 0U;
    uint32_t start;

    strip.y = client.y + FILES_MENU + FILES_TOOL;
    strip.height = FILES_PATH;
    chrome(surface, client, strip, window);
    pen = client.x + FILES_PAD;

    /* Walk the path one component at a time. The last one is the folder
     * you are in and is drawn pressed. */
    while (path[at] != '\0') {
        char name[RSD_FILES_NAME_BYTES];
        uint32_t length = 0U;
        struct rsd_rect button;
        bool last;

        while (path[at] == '/') {
            ++at;
        }
        if (path[at] == '\0') {
            break;
        }
        start = at;
        while (path[at] != '\0' && path[at] != '/'
               && length + 1U < sizeof(name)) {
            name[length] = path[at];
            ++length;
            ++at;
        }
        name[length] = '\0';
        (void)start;
        last = path[at] == '\0';

        button.x = pen;
        button.y = strip.y + 3U;
        button.width = rsd_font_width(name) + 14U;
        button.height = FILES_PATH - 7U;
        if (button.x + button.width > client.x + client.width) {
            break;
        }
        /*
         * ONLY THE ONE YOU ARE IN IS A BUTTON.
         *
         * Every component used to get a slab, which made the path read
         * as a row of tabs - and a row of tabs is a row of things that
         * all look equally where-you-are. Nautilus draws the folder you
         * are in pressed and leaves the rest of the path flat until you
         * point at it, so the row says where you are as well as how you
         * got there.
         */
        if (last) {
            rsd_oxy_button(surface, strip, button, RSD_BG, true);
        }
        rsd_font_draw(surface, strip, button.x + 7U, baseline(button),
                        name, last ? RSD_TEXT : RSD_FRAME_INK_DIM);
        pen = button.x + button.width + 3U;
    }

}

/*
 * THE PLACES LIST.
 *
 * Nautilus puts the same shortcuts down the left of every window and
 * people navigate by them rather than by walking the tree. The entries
 * are this machine's, not a copy of the reference's: there is no
 * cdrom1 under RSD and listing one would be furniture.
 */
static void draw_places(struct rsd_surface *surface,
    struct rsd_rect client, const struct rsd_window *window)
{
    static const struct { uint32_t icon; const char *name; } PLACES[] = {
        { RSD_ICON_PLACE_HOME,    "user" },
        { RSD_ICON_PLACE_DESKTOP, "Desktop" },
        { RSD_ICON_PLACE_DISK,    "File System" },
        { RSD_ICON_PLACE_NETWORK, "Network" },
        { RSD_ICON_PLACE_TRASH,   "Trash" },
    };
    struct rsd_rect side = client;
    uint32_t at;

    side.y = client.y + FILES_CHROME;
    side.width = FILES_SIDE;
    side.height = client.height > FILES_CHROME + FILES_STATUS ?
        client.height - FILES_CHROME - FILES_STATUS : 0U;
    if (side.height == 0U || client.width <= FILES_SIDE) {
        return;
    }
    chrome(surface, client, side, window);

    rsd_font_draw(surface, side, side.x + FILES_PAD, side.y + 14U,
                    "Places", RSD_FRAME_INK_DIM);

    for (at = 0U; at < sizeof(PLACES) / sizeof(PLACES[0]); ++at) {
        uint32_t row = side.y + 22U + at * 24U;

        if (row + 22U > side.y + side.height) {
            break;
        }
        rsd_oxy_blit(surface, side, side.x + FILES_PAD, row,
                       rsd_icons[PLACES[at].icon], RSD_ICON_SIZE);
        rsd_font_draw(surface, side, side.x + FILES_PAD
                        + RSD_ICON_SIZE + 6U, row + 15U,
                        PLACES[at].name, RSD_FG);
    }

    /* One line where the list meets the view, so the sidebar is a
     * region rather than a margin. */
    {
        uint32_t y;

        for (y = side.y; y < side.y + side.height; ++y) {
            rsd_surface_plot(surface, client,
                               side.x + side.width - 1U, y, RSD_LINE);
        }
    }
}

void rsd_files_draw(struct rsd_surface *surface,
    const struct rsd_window *window)
{
    struct rsd_rect client;
    struct rsd_rect box;
    char path[RSD_FILES_PATH_BYTES];

    if (window == NULL || !rsd_surface_valid(surface)) {
        return;
    }
    client = rsd_window_client(window);
    chrome(surface, client, client, window);

    rsd_files_path(here, path, sizeof(path));
    draw_menubar(surface, client, window);
    draw_toolbar(surface, client, window);
    draw_breadcrumbs(surface, client, path, window);
    draw_places(surface, client, window);

    box = view_area(window);
    rsd_surface_fill(surface, client, box, RSD_BASE);
    draw_entries(surface, window, box);
    /*
     * THE ONE LINE IN THE WINDOW. The view is sunk into the sheet, so
     * it gets the dark edge a sunken thing gets - and because it is the
     * only one, it is the only place the eye is told anything has
     * changed. Four rules across a window tell it nothing.
     */
    {
        uint32_t at;

        for (at = 0U; at < box.width; ++at) {
            rsd_surface_plot(surface, client, box.x + at, box.y - 1U,
                               rsd_oxy_dark(RSD_BG));
        }
        for (at = 0U; at < box.height + 1U; ++at) {
            rsd_surface_plot(surface, client, box.x - 1U,
                               box.y - 1U + at, rsd_oxy_dark(RSD_BG));
        }
    }
    draw_status(surface, client, window);
}

/*
 * The self test asks what a file manager has to get right and what a
 * picture of one cannot: does navigating MOVE, does Back go back without
 * turning into a toggle, and is a folder's size the sum of what is under
 * it rather than a number somebody typed?
 */
bool rsd_files_self_test(void)
{
    uint32_t home;
    uint32_t docs;
    uint32_t notes;

    rsd_files_reset();
    home = rsd_files_add(rsd_files_root(), "home", true, 0U);
    docs = rsd_files_add(home, "Documents", true, 0U);
    notes = rsd_files_add(docs, "Notes", true, 0U);
    if (home >= RSD_FILES_MAX_NODES || docs >= RSD_FILES_MAX_NODES ||
            notes >= RSD_FILES_MAX_NODES) {
        return false;
    }
    if (rsd_files_add(docs, "report.txt", false, 2000U) >=
            RSD_FILES_MAX_NODES) {
        return false;
    }
    if (rsd_files_add(notes, "todo.txt", false, 300U) >=
            RSD_FILES_MAX_NODES) {
        return false;
    }
    /* Counted, not stored: Documents holds 2000 plus the 300 under
     * Notes. */
    if (rsd_files_folder_bytes(docs) != 2300U) {
        return false;
    }
    if (!rsd_files_open(home) || rsd_files_here() != home) {
        return false;
    }
    if (!rsd_files_open(docs) || rsd_files_here() != docs) {
        return false;
    }
    if (!rsd_files_back() || rsd_files_here() != home) {
        return false;
    }
    /* Back again goes to where we started, NOT back to Documents: a Back
     * that pushes as it pops is a switch between two folders. */
    if (!rsd_files_back() || rsd_files_here() != rsd_files_root()) {
        return false;
    }
    if (!rsd_files_open(home)) {
        return false;
    }
    if (!rsd_files_up() || rsd_files_here() != rsd_files_root()) {
        return false;
    }
    /* The root has no parent, so Up refuses rather than walking off. */
    if (rsd_files_up()) {
        return false;
    }
    rsd_files_open(docs);
    rsd_files_select_all();
    if (rsd_files_selected_count() != rsd_files_child_count(docs)) {
        return false;
    }
    rsd_files_select(notes, true);
    if (rsd_files_is_selected(notes)) {
        return false;      /* ctrl on a selected item REMOVES it */
    }
    rsd_files_clear_selection();

    /* Dragging: report.txt out of Documents and into Notes. */
    {
        uint32_t report = rsd_files_child(docs, 1U);

        if (report >= RSD_FILES_MAX_NODES) {
            return false;
        }
        if (!rsd_files_move(report, notes)) {
            return false;
        }
        if (rsd_files_child_count(notes) != 2U) {
            return false;
        }
        /* And it is gone from where it was, not copied. */
        if (rsd_files_child_count(docs) != 1U) {
            return false;
        }
        /* The sizes follow, because they are counted: Notes now holds
         * both files and Documents holds only what is under Notes. */
        if (rsd_files_folder_bytes(notes) != 2300U) {
            return false;
        }
        /* Moving a folder INTO ITSELF is refused - the case that would
         * detach the subtree from the root. */
        if (rsd_files_move(docs, notes)) {
            return false;
        }
        /* And into its own current parent is refused, because it changes
         * nothing and would still cost a remove and an add. */
        if (rsd_files_move(notes, docs)) {
            return false;
        }
        /* A file is not a folder, so nothing can be moved into one. */
        if (rsd_files_move(notes, report)) {
            return false;
        }

        /* Rename, and what it refuses. */
        if (!rsd_files_rename(report, "summary.txt")) {
            return false;
        }
        if (!same(rsd_files_node_name(report), "summary.txt")) {
            return false;
        }
        /* Its own name is not a rename. */
        if (rsd_files_rename(report, "summary.txt")) {
            return false;
        }
        /* A name already in the folder is refused rather than making two
         * things with one name. */
        if (rsd_files_rename(report, "todo.txt")) {
            return false;
        }
        /* A path is not a name. */
        if (rsd_files_rename(report, "a/b")) {
            return false;
        }
        if (rsd_files_rename(report, "")) {
            return false;
        }

        /*
         * Removing a CHILD of the folder you are in is ordinary and must
         * work - the first version of this assertion had it backwards,
         * confusing "the folder you are looking at" with "anything under
         * it", and the self-test failed on its own bad expectation.
         */
        (void)rsd_files_open(docs);
        if (!rsd_files_remove(notes)) {
            return false;
        }
        if (rsd_files_here() != docs) {
            return false;
        }
    }
    {
        /* Deleting the folder you are LOOKING AT is refused. */
        uint32_t where = rsd_files_here();

        if (rsd_files_remove(where)) {
            return false;
        }
    }
    {
        /* And a real delete removes it and everything under it. */
        uint32_t root = rsd_files_root();
        uint32_t spare = rsd_files_add(root, "spare", true, 0U);
        uint32_t inside = rsd_files_add(spare, "deep.txt", false, 10U);
        uint32_t was = rsd_files_child_count(root);

        if (spare >= RSD_FILES_MAX_NODES ||
                inside >= RSD_FILES_MAX_NODES) {
            return false;
        }
        if (!rsd_files_remove(spare)) {
            return false;
        }
        if (rsd_files_child_count(root) != was - 1U) {
            return false;
        }
        /* The child went with it rather than being left unreachable. */
        if (rsd_files_child_count(spare) != 0U) {
            return false;
        }
    }

    /* The clipboard. */
    {
        uint32_t root = rsd_files_root();
        uint32_t box = rsd_files_add(root, "box", true, 0U);
        uint32_t leaf = rsd_files_add(box, "leaf.txt", false, 40U);
        uint32_t away = rsd_files_add(root, "away", true, 0U);
        uint32_t copied;

        if (box >= RSD_FILES_MAX_NODES ||
                leaf >= RSD_FILES_MAX_NODES ||
                away >= RSD_FILES_MAX_NODES) {
            return false;
        }
        (void)rsd_files_open(root);
        /* Nothing selected: nothing to copy. */
        rsd_files_clear_selection();
        if (rsd_files_copy_selection(false)) {
            return false;
        }
        rsd_files_select(box, false);
        if (!rsd_files_copy_selection(false)) {
            return false;
        }
        if (!rsd_files_clipboard_has()) {
            return false;
        }
        if (rsd_files_paste_into(away) != 1U) {
            return false;
        }
        /* The CHILD came with it, as a new node rather than a shared
         * one - so emptying the copy must not empty the original. */
        copied = rsd_files_child(away, 0U);
        if (copied >= RSD_FILES_MAX_NODES) {
            return false;
        }
        if (rsd_files_child_count(copied) != 1U) {
            return false;
        }
        if (rsd_files_child(copied, 0U) == leaf) {
            return false;       /* shared, not cloned */
        }
        if (!rsd_files_remove(rsd_files_child(copied, 0U))) {
            return false;
        }
        if (rsd_files_child_count(box) != 1U) {
            return false;       /* the original lost its child */
        }
        /* A COPY is not spent: pasting again gives a "(copy)". */
        if (rsd_files_paste_into(away) != 1U) {
            return false;
        }
        if (rsd_files_child_count(away) != 2U) {
            return false;
        }
        /* A CUT is spent, and moves rather than duplicates. */
        rsd_files_select(box, false);
        if (!rsd_files_copy_selection(true)) {
            return false;
        }
        if (rsd_files_paste_into(away) != 1U) {
            return false;
        }
        if (rsd_files_clipboard_has()) {
            return false;
        }
        /* Pasting a folder INTO ITSELF is refused. */
        rsd_files_select(away, false);
        if (!rsd_files_copy_selection(false)) {
            return false;
        }
        if (rsd_files_paste_into(away) != 0U) {
            return false;
        }
    }
    return true;
}
