/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/de/files.h>

#include <trait/de/font.h>
#include <trait/de/theme.h>

#include "de_trait_files_art.h"

/* ================================================================ METRICS
 *
 * the Trait OS desktop file view.
 */
#define FILES_MENUBAR 20U
#define FILES_TOOLBAR 28U
#define FILES_STATUS 20U
#define FILES_PLACES 130U
#define FILES_PAD 6U

#define FILES_CELL_WIDTH 86U        /* the icon view's grid */
#define FILES_CELL_HEIGHT 72U
#define FILES_ICON 48U
#define FILES_SMALL 16U

#define FILES_ROW 18U               /* the detailed list */
#define FILES_NAME_COLUMN 220U
#define FILES_KIND_COLUMN 120U
#define FILES_SIZE_COLUMN 90U

/* ================================================================== STATE */

static struct trait_files_node nodes[TRAIT_FILES_MAX_NODES];
static uint32_t node_count;
static uint32_t children[TRAIT_FILES_MAX_NODES][TRAIT_FILES_MAX_CHILDREN];
static uint32_t child_counts[TRAIT_FILES_MAX_NODES];

static uint32_t here;
static uint32_t history[16];
static uint32_t history_depth;

static uint32_t selected[TRAIT_FILES_MAX_SELECTED];
static uint32_t selected_count;
static enum trait_files_view view_mode = TRAIT_FILES_ICONS;

static uint32_t clipboard[TRAIT_FILES_MAX_SELECTED];
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
static const char *mark_for(const struct trait_files_node *node)
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

static const char *kind_for(const struct trait_files_node *node)
{
    if (node->folder) {
        return "Folder";
    }
    if (ends_with(node->name, ".txt")) {
        return "Plain text";
    }
    if (ends_with(node->name, ".png")) {
        return "PNG image";
    }
    if (ends_with(node->name, ".ogg")) {
        return "Ogg audio";
    }
    return "File";
}

static const struct trait_files_art_entry *art_named(const char *name)
{
    uint32_t at;
    uint32_t index;

    for (index = 0U; index < TRAIT_FILES_ART_COUNT; ++index) {
        const char *candidate = trait_files_art[index].name;

        for (at = 0U; ; ++at) {
            if (candidate[at] != name[at]) {
                break;
            }
            if (candidate[at] == '\0') {
                return &trait_files_art[index];
            }
        }
    }
    return NULL;
}

static uint32_t art_plane(uint32_t size)
{
    uint32_t at;

    for (at = 0U; at < TRAIT_FILES_ART_SIZES; ++at) {
        if (trait_files_art_size[at] == size) {
            return at;
        }
    }
    return TRAIT_FILES_ART_SIZES;
}

static void draw_icon(struct trait_surface *surface, struct trait_rect clip,
    const char *name, uint32_t size, uint32_t left, uint32_t top)
{
    const struct trait_files_art_entry *art = art_named(name);
    uint32_t plane = art_plane(size);
    uint32_t x;
    uint32_t y;

    if (art == NULL || plane >= TRAIT_FILES_ART_SIZES) {
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
            under = trait_surface_read(surface, left + x, top + y);
            trait_surface_plot(surface, clip, left + x, top + y,
                trait_blend(under, art->pixels[plane][at], alpha));
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
        uint32_t tenths = (bytes / 1048576U) * 10U +
            ((bytes % 1048576U) * 10U) / 1048576U;

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

const char *trait_files_node_name(uint32_t node)
{
    if (node >= node_count) {
        return "";
    }
    return nodes[node].name;
}

const char *trait_files_node_mark(uint32_t node)
{
    if (node >= node_count) {
        return "text-x-generic";
    }
    return mark_for(&nodes[node]);
}

void trait_files_draw_icon_at(struct trait_surface *surface,
    struct trait_rect clip, const char *mark, uint32_t size,
    uint32_t left, uint32_t top)
{
    draw_icon(surface, clip, mark, size, left, top);
}

void trait_files_reset(void)
{
    uint32_t at;

    clip_count = 0U;
    clip_cut = false;
    node_count = 0U;
    for (at = 0U; at < TRAIT_FILES_MAX_NODES; ++at) {
        child_counts[at] = 0U;
    }
    selected_count = 0U;
    history_depth = 0U;
    copy(nodes[0].name, "/", TRAIT_FILES_NAME_BYTES);
    nodes[0].folder = true;
    nodes[0].bytes = 0U;
    nodes[0].parent = TRAIT_FILES_MAX_NODES;
    node_count = 1U;
    here = 0U;
}

uint32_t trait_files_root(void)
{
    return 0U;
}

uint32_t trait_files_add(uint32_t parent, const char *name, bool folder,
    uint32_t bytes)
{
    uint32_t index;

    if (node_count >= TRAIT_FILES_MAX_NODES || name == NULL ||
            parent >= node_count || !nodes[parent].folder) {
        return TRAIT_FILES_MAX_NODES;
    }
    if (child_counts[parent] >= TRAIT_FILES_MAX_CHILDREN) {
        return TRAIT_FILES_MAX_NODES;
    }
    index = node_count++;
    copy(nodes[index].name, name, TRAIT_FILES_NAME_BYTES);
    nodes[index].folder = folder;
    nodes[index].bytes = folder ? 0U : bytes;
    nodes[index].parent = parent;
    children[parent][child_counts[parent]++] = index;
    return index;
}

uint32_t trait_files_child_count(uint32_t folder)
{
    if (folder >= node_count) {
        return 0U;
    }
    return child_counts[folder];
}

uint32_t trait_files_child(uint32_t folder, uint32_t at)
{
    if (folder >= node_count || at >= child_counts[folder]) {
        return TRAIT_FILES_MAX_NODES;
    }
    return children[folder][at];
}

/*
 * COUNTED, NOT STORED.  A folder's size is whatever is under it right
 * now, so it cannot drift from the truth the way a cached number does -
 * and the status bar can stand behind what it prints.
 */
uint32_t trait_files_folder_bytes(uint32_t folder)
{
    uint32_t total = 0U;
    uint32_t at;

    if (folder >= node_count) {
        return 0U;
    }
    for (at = 0U; at < child_counts[folder]; ++at) {
        uint32_t child = children[folder][at];

        total += nodes[child].folder ?
            trait_files_folder_bytes(child) : nodes[child].bytes;
    }
    return total;
}

void trait_files_path(uint32_t node, char *out, uint32_t capacity)
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

bool trait_files_open(uint32_t folder)
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

uint32_t trait_files_here(void)
{
    return here;
}

bool trait_files_up(void)
{
    if (nodes[here].parent >= node_count) {
        return false;
    }
    return trait_files_open(nodes[here].parent);
}

bool trait_files_back(void)
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

bool trait_files_is_selected(uint32_t node)
{
    uint32_t at;

    for (at = 0U; at < selected_count; ++at) {
        if (selected[at] == node) {
            return true;
        }
    }
    return false;
}

void trait_files_select(uint32_t node, bool add)
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
    if (trait_files_is_selected(node)) {
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
    if (selected_count < TRAIT_FILES_MAX_SELECTED) {
        selected[selected_count++] = node;
    }
}

void trait_files_select_all(void)
{
    uint32_t at;

    selected_count = 0U;
    for (at = 0U; at < child_counts[here] &&
            selected_count < TRAIT_FILES_MAX_SELECTED; ++at) {
        selected[selected_count++] = children[here][at];
    }
}

void trait_files_clear_selection(void)
{
    selected_count = 0U;
}

uint32_t trait_files_selected_count(void)
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
    return at + 1U < TRAIT_FILES_NAME_BYTES;
}

bool trait_files_name_free(uint32_t folder, const char *name)
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

bool trait_files_copy_selection(bool cut)
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

bool trait_files_clipboard_has(void)
{
    return clip_count != 0U;
}

bool trait_files_clipboard_is_cut(void)
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

    copy(out, name, TRAIT_FILES_NAME_BYTES);
    if (trait_files_name_free(folder, out)) {
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

        for (from = 0U; from < dot && put + 1U < TRAIT_FILES_NAME_BYTES;
                ++from) {
            out[put++] = name[from];
        }
        {
            static const char TAG[] = " (copy";
            uint32_t tag = 0U;

            while (TAG[tag] != '\0' &&
                    put + 1U < TRAIT_FILES_NAME_BYTES) {
                out[put++] = TAG[tag++];
            }
        }
        if (nth > 1U && put + 3U < TRAIT_FILES_NAME_BYTES) {
            out[put++] = ' ';
            out[put++] = (char)('0' + nth);
        }
        if (put + 1U < TRAIT_FILES_NAME_BYTES) {
            out[put++] = ')';
        }
        for (from = dot; name[from] != '\0' &&
                put + 1U < TRAIT_FILES_NAME_BYTES; ++from) {
            out[put++] = name[from];
        }
        out[put] = '\0';
        if (trait_files_name_free(folder, out)) {
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
    uint32_t made = trait_files_add(folder, as_name, nodes[node].folder,
                                    nodes[node].bytes);
    uint32_t at;

    if (made >= TRAIT_FILES_MAX_NODES) {
        return TRAIT_FILES_MAX_NODES;
    }
    for (at = 0U; at < child_counts[node]; ++at) {
        uint32_t child = children[node][at];

        if (clone_into(child, made, nodes[child].name) >=
                TRAIT_FILES_MAX_NODES) {
            return TRAIT_FILES_MAX_NODES;
        }
    }
    return made;
}

uint32_t trait_files_paste_into(uint32_t folder)
{
    char name[TRAIT_FILES_NAME_BYTES];
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
        if (node == folder || trait_files_is_inside(folder, node)) {
            continue;
        }
        if (clip_cut) {
            if (nodes[node].parent == folder) {
                continue;
            }
            if (trait_files_move(node, folder)) {
                ++done;
            }
            continue;
        }
        unique_name(folder, nodes[node].name, name);
        if (clone_into(node, folder, name) < TRAIT_FILES_MAX_NODES) {
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

bool trait_files_rename(uint32_t node, const char *name)
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
    if (!trait_files_name_free(nodes[node].parent, name)) {
        return false;
    }
    copy(nodes[node].name, name, TRAIT_FILES_NAME_BYTES);
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
    nodes[node].parent = TRAIT_FILES_MAX_NODES;
}

bool trait_files_remove(uint32_t node)
{
    uint32_t parent;

    if (node == 0U || node >= node_count) {
        return false;
    }
    /* Not the folder you are looking at, and not one you are inside:
     * either leaves the window showing something that is gone. */
    if (trait_files_is_inside(here, node)) {
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

bool trait_files_is_inside(uint32_t node, uint32_t maybe_ancestor)
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

bool trait_files_move(uint32_t node, uint32_t into)
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
    if (trait_files_is_inside(into, node)) {
        return false;
    }
    from = nodes[node].parent;
    if (from == into) {
        return false;
    }
    if (from >= node_count ||
            child_counts[into] >= TRAIT_FILES_MAX_CHILDREN) {
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

void trait_files_set_view(enum trait_files_view view)
{
    view_mode = view;
}

enum trait_files_view trait_files_view_mode(void)
{
    return view_mode;
}

/* ================================================================= LAYOUT */

static struct trait_rect view_area(const struct trait_window *window)
{
    struct trait_rect client = trait_window_client(window);
    struct trait_rect box;

    box.x = client.x + FILES_PLACES;
    box.y = client.y + FILES_MENUBAR + FILES_TOOLBAR;
    box.width = client.width > FILES_PLACES ?
        client.width - FILES_PLACES : 0U;
    box.height = client.height > FILES_MENUBAR + FILES_TOOLBAR +
        FILES_STATUS ?
        client.height - FILES_MENUBAR - FILES_TOOLBAR - FILES_STATUS : 0U;
    return box;
}

bool trait_files_entry_bounds(const struct trait_window *window,
    uint32_t at, struct trait_rect *out)
{
    struct trait_rect box;
    uint32_t columns;

    if (window == NULL || out == NULL || at >= child_counts[here]) {
        return false;
    }
    box = view_area(window);
    if (view_mode == TRAIT_FILES_LIST) {
        out->x = box.x;
        out->y = box.y + FILES_ROW + at * FILES_ROW;
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

/* ================================================================ DRAWING */

static void frame_line(struct trait_surface *surface, struct trait_rect clip,
    uint32_t x, uint32_t y, uint32_t length, bool vertical, uint32_t ink)
{
    uint32_t at;

    for (at = 0U; at < length; ++at) {
        trait_surface_plot(surface, clip, vertical ? x : x + at,
                           vertical ? y + at : y, ink);
    }
}

static void draw_places(struct trait_surface *surface,
    struct trait_rect client)
{
    static const char *const PLACES[4] = {
        "user", "Desktop", "Trash", "Filesystem"
    };
    static const char *const MARKS[4] = {
        "user-home", "user-desktop", "user-trash", "drive-harddisk"
    };
    struct trait_rect pane;
    uint32_t at;

    pane.x = client.x;
    pane.y = client.y + FILES_MENUBAR + FILES_TOOLBAR;
    pane.width = FILES_PLACES;
    pane.height = client.height > FILES_MENUBAR + FILES_TOOLBAR +
        FILES_STATUS ?
        client.height - FILES_MENUBAR - FILES_TOOLBAR - FILES_STATUS : 0U;
    trait_surface_fill(surface, client, pane, TRAIT_BG);
    frame_line(surface, client, pane.x + pane.width - 1U, pane.y,
               pane.height, true, TRAIT_LINE);
    for (at = 0U; at < 4U; ++at) {
        uint32_t top = pane.y + 4U + at * 22U;

        draw_icon(surface, pane, MARKS[at], FILES_SMALL,
                  pane.x + 8U, top + 2U);
        trait_font_draw(surface, pane, pane.x + 30U, top + 14U,
                        PLACES[at], TRAIT_FG);
    }
}

static void draw_toolbar(struct trait_surface *surface,
    struct trait_rect client)
{
    struct trait_rect strip;
    char path[TRAIT_FILES_PATH_BYTES];
    struct trait_rect field;
    uint32_t at;

    strip = client;
    strip.y = client.y + FILES_MENUBAR;
    strip.height = FILES_TOOLBAR;
    trait_surface_fill(surface, client, strip, TRAIT_BG);
    frame_line(surface, client, strip.x, strip.y + strip.height - 1U,
               strip.width, false, TRAIT_LINE);

    /* The location bar, which is a sunken entry rather than a label: it
     * is where the path is READ from, and a flat label would be saying
     * this window has one when it has not. */
    field.x = strip.x + FILES_PAD;
    field.y = strip.y + 4U;
    field.width = strip.width > FILES_PAD * 2U ?
        strip.width - FILES_PAD * 2U : 0U;
    field.height = 19U;
    trait_surface_fill(surface, client, field, TRAIT_BASE);
    frame_line(surface, client, field.x, field.y, field.width, false,
               TRAIT_LINE);
    frame_line(surface, client, field.x, field.y, field.height, true,
               TRAIT_LINE);
    frame_line(surface, client, field.x, field.y + field.height - 1U,
               field.width, false, TRAIT_LINE_LIGHT);
    frame_line(surface, client, field.x + field.width - 1U, field.y,
               field.height, true, TRAIT_LINE_LIGHT);
    trait_files_path(here, path, sizeof(path));
    trait_font_draw(surface, field, field.x + 5U, field.y + 13U,
                    path, TRAIT_TEXT);
    (void)at;
}

static void draw_status(struct trait_surface *surface,
    struct trait_rect client)
{
    struct trait_rect strip;
    char left[48];
    char right[48];
    uint32_t width;

    strip.x = client.x;
    strip.y = client.y + client.height - FILES_STATUS;
    strip.width = client.width;
    strip.height = FILES_STATUS;
    trait_surface_fill(surface, client, strip, TRAIT_BG);
    frame_line(surface, client, strip.x, strip.y, strip.width, false,
               TRAIT_LINE);

    left[0] = '\0';
    if (selected_count > 1U) {
        uint32_t bytes = 0U;
        uint32_t at;

        for (at = 0U; at < selected_count; ++at) {
            bytes += nodes[selected[at]].folder ?
                trait_files_folder_bytes(selected[at]) :
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
              trait_files_folder_bytes(selected[0]) :
              nodes[selected[0]].bytes, sizeof(size));
        append(left, size, sizeof(left));
        append(left, ") selected", sizeof(left));
    } else {
        (void)number(left, child_counts[here], sizeof(left));
        append(left, child_counts[here] == 1U ? " item" : " items",
               sizeof(left));
    }
    trait_font_draw(surface, strip, strip.x + FILES_PAD,
                    strip.y + 14U, left, TRAIT_TEXT);

    /*
     * The right-hand field is what is IN THIS FOLDER, counted.  pcmanfm
     * puts free space there; there is no filesystem under this one to ask,
     * and a status bar that makes up a figure is worse than one that
     * leaves the field out.
     */
    right[0] = '\0';
    human(right, trait_files_folder_bytes(here), sizeof(right));
    append(right, " in this folder", sizeof(right));
    width = trait_font_width(right);
    if (strip.width > width + FILES_PAD) {
        trait_font_draw(surface, strip,
            strip.x + strip.width - width - FILES_PAD, strip.y + 14U,
            right, TRAIT_TEXT);
    }
}

static void draw_entries(struct trait_surface *surface,
    const struct trait_window *window, struct trait_rect box)
{
    struct trait_rect cell;
    uint32_t at;

    if (view_mode == TRAIT_FILES_LIST) {
        static const char *const HEADS[3] = { "Name", "Description",
                                              "Size" };
        static const uint32_t WIDTHS[3] = {
            FILES_NAME_COLUMN, FILES_KIND_COLUMN, FILES_SIZE_COLUMN
        };
        uint32_t left = box.x;

        for (at = 0U; at < 3U; ++at) {
            struct trait_rect head;

            head.x = left;
            head.y = box.y;
            head.width = WIDTHS[at];
            head.height = FILES_ROW;
            trait_surface_fill(surface, box, head, TRAIT_BG_ACTIVE);
            frame_line(surface, box, head.x + head.width - 1U, head.y,
                       head.height, true, TRAIT_LINE);
            frame_line(surface, box, head.x, head.y + head.height - 1U,
                       head.width, false, TRAIT_LINE);
            trait_font_draw(surface, head, head.x + 5U, head.y + 13U,
                            HEADS[at], TRAIT_FG);
            left += WIDTHS[at];
        }
    }

    for (at = 0U; at < child_counts[here]; ++at) {
        uint32_t node = children[here][at];
        bool lit = trait_files_is_selected(node);

        if (!trait_files_entry_bounds(window, at, &cell)) {
            continue;
        }
        if (cell.y + cell.height > box.y + box.height) {
            break;
        }
        if (view_mode == TRAIT_FILES_LIST) {
            char size[24];

            if (lit) {
                trait_surface_fill(surface, box, cell, TRAIT_SEL_BG);
            } else if ((at & 1U) != 0U) {
                trait_surface_fill(surface, box, cell,
                                   TRAIT_BASE_PRELIGHT);
            }
            draw_icon(surface, box, mark_for(&nodes[node]), FILES_SMALL,
                      cell.x + 4U, cell.y + 1U);
            trait_font_draw(surface, box, cell.x + 24U, cell.y + 13U,
                nodes[node].name, lit ? TRAIT_SEL_FG : TRAIT_TEXT);
            trait_font_draw(surface, box,
                cell.x + FILES_NAME_COLUMN + 5U, cell.y + 13U,
                kind_for(&nodes[node]), lit ? TRAIT_SEL_FG : TRAIT_TEXT);
            /* A FOLDER HAS NO SIZE in this column, and an empty cell says
             * so better than a nought does. */
            if (!nodes[node].folder) {
                human(size, nodes[node].bytes, sizeof(size));
                trait_font_draw(surface, box,
                    cell.x + FILES_NAME_COLUMN + FILES_KIND_COLUMN + 5U,
                    cell.y + 13U, size,
                    lit ? TRAIT_SEL_FG : TRAIT_TEXT);
            }
            continue;
        }

        if (lit) {
            struct trait_rect wash = cell;

            wash.x += 2U;
            wash.width -= 4U;
            trait_surface_fill(surface, box, wash, TRAIT_SEL_BG);
        }
        draw_icon(surface, box, mark_for(&nodes[node]), FILES_ICON,
                  cell.x + (cell.width - FILES_ICON) / 2U, cell.y + 4U);
        {
            uint32_t width = trait_font_width(nodes[node].name);
            uint32_t pen = cell.x + (cell.width > width ?
                (cell.width - width) / 2U : 0U);

            trait_font_draw(surface, box, pen, cell.y + FILES_ICON + 18U,
                nodes[node].name, lit ? TRAIT_SEL_FG : TRAIT_FG);
        }
    }
}

void trait_files_draw(struct trait_surface *surface,
    const struct trait_window *window)
{
    static const char *const MENUS[6] = {
        "File", "Edit", "View", "Bookmarks", "Tools", "Help"
    };
    struct trait_rect client;
    struct trait_rect box;
    uint32_t pen;
    uint32_t at;

    if (window == NULL || !trait_surface_valid(surface)) {
        return;
    }
    client = trait_window_client(window);
    trait_surface_fill(surface, client, client, TRAIT_BG);

    pen = client.x + FILES_PAD;
    for (at = 0U; at < 6U; ++at) {
        trait_font_draw(surface, client, pen, client.y + 14U,
                        MENUS[at], TRAIT_FG);
        pen += trait_font_width(MENUS[at]) + 14U;
    }
    frame_line(surface, client, client.x, client.y + FILES_MENUBAR - 1U,
               client.width, false, TRAIT_LINE);

    draw_toolbar(surface, client);
    draw_places(surface, client);

    box = view_area(window);
    trait_surface_fill(surface, client, box, TRAIT_BASE);
    draw_entries(surface, window, box);
    draw_status(surface, client);
}

/*
 * The self test asks what a file manager has to get right and what a
 * picture of one cannot: does navigating MOVE, does Back go back without
 * turning into a toggle, and is a folder's size the sum of what is under
 * it rather than a number somebody typed?
 */
bool trait_files_self_test(void)
{
    uint32_t home;
    uint32_t docs;
    uint32_t notes;
    struct trait_rect ignored;
    char size[24];

    trait_files_reset();
    if (trait_files_entry_bounds(NULL, 0U, &ignored)) {
        return false;
    }
    human(size, 1048576U, sizeof(size));
    if (!same(size, "1.0 MiB")) {
        return false;
    }
    home = trait_files_add(trait_files_root(), "home", true, 0U);
    docs = trait_files_add(home, "Documents", true, 0U);
    notes = trait_files_add(docs, "Notes", true, 0U);
    if (home >= TRAIT_FILES_MAX_NODES || docs >= TRAIT_FILES_MAX_NODES ||
            notes >= TRAIT_FILES_MAX_NODES) {
        return false;
    }
    if (trait_files_add(docs, "report.txt", false, 2000U) >=
            TRAIT_FILES_MAX_NODES) {
        return false;
    }
    if (trait_files_add(notes, "todo.txt", false, 300U) >=
            TRAIT_FILES_MAX_NODES) {
        return false;
    }
    /* Counted, not stored: Documents holds 2000 plus the 300 under
     * Notes. */
    if (trait_files_folder_bytes(docs) != 2300U) {
        return false;
    }
    if (!trait_files_open(home) || trait_files_here() != home) {
        return false;
    }
    if (!trait_files_open(docs) || trait_files_here() != docs) {
        return false;
    }
    if (!trait_files_back() || trait_files_here() != home) {
        return false;
    }
    /* Back again goes to where we started, NOT back to Documents: a Back
     * that pushes as it pops is a switch between two folders. */
    if (!trait_files_back() || trait_files_here() != trait_files_root()) {
        return false;
    }
    if (!trait_files_open(home)) {
        return false;
    }
    if (!trait_files_up() || trait_files_here() != trait_files_root()) {
        return false;
    }
    /* The root has no parent, so Up refuses rather than walking off. */
    if (trait_files_up()) {
        return false;
    }
    trait_files_open(docs);
    trait_files_select_all();
    if (trait_files_selected_count() != trait_files_child_count(docs)) {
        return false;
    }
    trait_files_select(notes, true);
    if (trait_files_is_selected(notes)) {
        return false;      /* ctrl on a selected item REMOVES it */
    }
    trait_files_clear_selection();

    /* Dragging: report.txt out of Documents and into Notes. */
    {
        uint32_t report = trait_files_child(docs, 1U);

        if (report >= TRAIT_FILES_MAX_NODES) {
            return false;
        }
        if (!trait_files_move(report, notes)) {
            return false;
        }
        if (trait_files_child_count(notes) != 2U) {
            return false;
        }
        /* And it is gone from where it was, not copied. */
        if (trait_files_child_count(docs) != 1U) {
            return false;
        }
        /* The sizes follow, because they are counted: Notes now holds
         * both files and Documents holds only what is under Notes. */
        if (trait_files_folder_bytes(notes) != 2300U) {
            return false;
        }
        /* Moving a folder INTO ITSELF is refused - the case that would
         * detach the subtree from the root. */
        if (trait_files_move(docs, notes)) {
            return false;
        }
        /* And into its own current parent is refused, because it changes
         * nothing and would still cost a remove and an add. */
        if (trait_files_move(notes, docs)) {
            return false;
        }
        /* A file is not a folder, so nothing can be moved into one. */
        if (trait_files_move(notes, report)) {
            return false;
        }

        /* Rename, and what it refuses. */
        if (!trait_files_rename(report, "summary.txt")) {
            return false;
        }
        if (!same(trait_files_node_name(report), "summary.txt")) {
            return false;
        }
        /* Its own name is not a rename. */
        if (trait_files_rename(report, "summary.txt")) {
            return false;
        }
        /* A name already in the folder is refused rather than making two
         * things with one name. */
        if (trait_files_rename(report, "todo.txt")) {
            return false;
        }
        /* A path is not a name. */
        if (trait_files_rename(report, "a/b")) {
            return false;
        }
        if (trait_files_rename(report, "")) {
            return false;
        }

        /*
         * Removing a CHILD of the folder you are in is ordinary and must
         * work - the first version of this assertion had it backwards,
         * confusing "the folder you are looking at" with "anything under
         * it", and the self-test failed on its own bad expectation.
         */
        (void)trait_files_open(docs);
        if (!trait_files_remove(notes)) {
            return false;
        }
        if (trait_files_here() != docs) {
            return false;
        }
    }
    {
        /* Deleting the folder you are LOOKING AT is refused. */
        uint32_t where = trait_files_here();

        if (trait_files_remove(where)) {
            return false;
        }
    }
    {
        /* And a real delete removes it and everything under it. */
        uint32_t root = trait_files_root();
        uint32_t spare = trait_files_add(root, "spare", true, 0U);
        uint32_t inside = trait_files_add(spare, "deep.txt", false, 10U);
        uint32_t was = trait_files_child_count(root);

        if (spare >= TRAIT_FILES_MAX_NODES ||
                inside >= TRAIT_FILES_MAX_NODES) {
            return false;
        }
        if (!trait_files_remove(spare)) {
            return false;
        }
        if (trait_files_child_count(root) != was - 1U) {
            return false;
        }
        /* The child went with it rather than being left unreachable. */
        if (trait_files_child_count(spare) != 0U) {
            return false;
        }
    }

    /* The clipboard. */
    {
        uint32_t root = trait_files_root();
        uint32_t box = trait_files_add(root, "box", true, 0U);
        uint32_t leaf = trait_files_add(box, "leaf.txt", false, 40U);
        uint32_t away = trait_files_add(root, "away", true, 0U);
        uint32_t copied;

        if (box >= TRAIT_FILES_MAX_NODES ||
                leaf >= TRAIT_FILES_MAX_NODES ||
                away >= TRAIT_FILES_MAX_NODES) {
            return false;
        }
        (void)trait_files_open(root);
        /* Nothing selected: nothing to copy. */
        trait_files_clear_selection();
        if (trait_files_copy_selection(false)) {
            return false;
        }
        trait_files_select(box, false);
        if (!trait_files_copy_selection(false)) {
            return false;
        }
        if (!trait_files_clipboard_has()) {
            return false;
        }
        if (trait_files_paste_into(away) != 1U) {
            return false;
        }
        /* The CHILD came with it, as a new node rather than a shared
         * one - so emptying the copy must not empty the original. */
        copied = trait_files_child(away, 0U);
        if (copied >= TRAIT_FILES_MAX_NODES) {
            return false;
        }
        if (trait_files_child_count(copied) != 1U) {
            return false;
        }
        if (trait_files_child(copied, 0U) == leaf) {
            return false;       /* shared, not cloned */
        }
        if (!trait_files_remove(trait_files_child(copied, 0U))) {
            return false;
        }
        if (trait_files_child_count(box) != 1U) {
            return false;       /* the original lost its child */
        }
        /* A COPY is not spent: pasting again gives a "(copy)". */
        if (trait_files_paste_into(away) != 1U) {
            return false;
        }
        if (trait_files_child_count(away) != 2U) {
            return false;
        }
        /* A CUT is spent, and moves rather than duplicates. */
        trait_files_select(box, false);
        if (!trait_files_copy_selection(true)) {
            return false;
        }
        if (trait_files_paste_into(away) != 1U) {
            return false;
        }
        if (trait_files_clipboard_has()) {
            return false;
        }
        /* Pasting a folder INTO ITSELF is refused. */
        trait_files_select(away, false);
        if (!trait_files_copy_selection(false)) {
            return false;
        }
        if (trait_files_paste_into(away) != 0U) {
            return false;
        }
    }
    return true;
}
