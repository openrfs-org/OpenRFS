/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/sys.h>

#include <rsd/term.h>

static struct rsd_stage stages[RSD_LEDGER_MAX];
static uint32_t stage_count;

static struct rsd_node nodes[RSD_NODES];
static uint32_t node_count;

static char user_name[RSD_NAME_MAX];
static bool user_set;

static struct rsd_pkg pkgs[RSD_PKGS];
static uint32_t pkg_count;

void rsd_sys_reset(void)
{
    stage_count = 0U;
    node_count = 0U;
    user_set = false;
    user_name[0] = '\0';
    pkg_count = 0U;
    /* Node 0 is the root and always exists. */
    nodes[0].name[0] = '/';
    nodes[0].name[1] = '\0';
    nodes[0].parent = 0U;
    nodes[0].dir = true;
    nodes[0].bytes = 0U;
    nodes[0].used = true;
    node_count = 1U;
}

bool rsd_sys_add_stage(const char *name, const char *note,
                        enum rsd_stage_verdict verdict, uint32_t us)
{
    if (name == NULL || stage_count >= RSD_LEDGER_MAX) {
        return false;
    }
    (void)rsd_strcopy(stages[stage_count].name, RSD_NAME_MAX, name);
    (void)rsd_strcopy(stages[stage_count].note, RSD_TEXT,
                       note != NULL ? note : "");
    stages[stage_count].verdict = verdict;
    stages[stage_count].us = us;
    ++stage_count;
    return true;
}

uint32_t rsd_sys_stage_count(void)
{
    return stage_count;
}

const struct rsd_stage *rsd_sys_stage(uint32_t at)
{
    return at < stage_count ? &stages[at] : (const struct rsd_stage *)0;
}

uint32_t rsd_sys_verified_count(void)
{
    uint32_t at;
    uint32_t n = 0U;

    for (at = 0U; at < stage_count; ++at) {
        if (stages[at].verdict == RSD_STAGE_VERIFIED) {
            ++n;
        }
    }
    return n;
}

uint32_t rsd_fs_root(void)
{
    return 0U;
}

uint32_t rsd_fs_add(uint32_t parent, const char *name, bool dir,
                     uint32_t bytes)
{
    uint32_t at;

    if (name == NULL || node_count >= RSD_NODES || parent >= node_count) {
        return RSD_NODES;
    }
    at = node_count++;
    (void)rsd_strcopy(nodes[at].name, RSD_FNAME, name);
    nodes[at].parent = parent;
    nodes[at].dir = dir;
    nodes[at].bytes = dir ? 0U : bytes;
    nodes[at].used = true;
    return at;
}

uint32_t rsd_fs_count(void)
{
    return node_count;
}

const struct rsd_node *rsd_fs_node(uint32_t at)
{
    if (at >= node_count || !nodes[at].used) {
        return (const struct rsd_node *)0;
    }
    return &nodes[at];
}

/*
 * FAT32 8.3 names are case-insensitive. A lookup that was case-sensitive
 * would let you cd into a directory you cannot then list.
 */
static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool name_eq(const char *a, const char *b)
{
    uint32_t at = 0U;

    while (a[at] != '\0' && b[at] != '\0') {
        if (lower(a[at]) != lower(b[at])) {
            return false;
        }
        ++at;
    }
    return a[at] == b[at];
}

uint32_t rsd_fs_find(uint32_t parent, const char *name)
{
    uint32_t at;

    if (name == NULL) {
        return RSD_NODES;
    }
    for (at = 0U; at < node_count; ++at) {
        if (nodes[at].used && at != 0U && nodes[at].parent == parent &&
                name_eq(nodes[at].name, name)) {
            return at;
        }
    }
    return RSD_NODES;
}

uint32_t rsd_fs_parent(uint32_t node)
{
    if (node >= node_count || !nodes[node].used) {
        return 0U;
    }
    return nodes[node].parent;
}

void rsd_fs_path(uint32_t node, char *out, uint32_t capacity)
{
    uint32_t chain[12];
    uint32_t depth = 0U;
    uint32_t at = 0U;

    if (out == NULL || capacity == 0U) {
        return;
    }
    out[0] = '\0';
    if (node >= node_count || !nodes[node].used) {
        return;
    }
    while (node != 0U && depth < 12U) {
        chain[depth++] = node;
        node = nodes[node].parent;
    }
    if (depth == 0U) {
        (void)rsd_strcopy(out, capacity, "/");
        return;
    }
    while (depth-- != 0U) {
        if (at + 1U < capacity) {
            out[at++] = '/';
        }
        for (uint32_t i = 0U; nodes[chain[depth]].name[i] != '\0' &&
                 at + 1U < capacity; ++i) {
            out[at++] = nodes[chain[depth]].name[i];
        }
    }
    out[at] = '\0';
}

/*
 * Removing a directory removes what is under it. Leaving orphans
 * reachable by index would be worse than refusing.
 */
bool rsd_fs_remove(uint32_t node)
{
    uint32_t at;

    if (node == 0U || node >= node_count || !nodes[node].used) {
        return false;
    }
    for (at = 0U; at < node_count; ++at) {
        if (nodes[at].used && at != node && nodes[at].parent == node) {
            (void)rsd_fs_remove(at);
        }
    }
    nodes[node].used = false;
    return true;
}

bool rsd_user_exists(void)
{
    return user_set;
}

const char *rsd_user_name(void)
{
    return user_set ? user_name : "";
}

bool rsd_user_add(const char *name)
{
    uint32_t n;

    if (name == NULL || user_set) {
        return false;
    }
    n = rsd_strlen(name);
    if (n == 0U || n > 31U) {
        return false;
    }
    /* Must begin with a letter or digit; letters, digits, - and _ after. */
    if (!((name[0] >= 'a' && name[0] <= 'z') ||
          (name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= '0' && name[0] <= '9'))) {
        return false;
    }
    for (uint32_t at = 0U; at < n; ++at) {
        char c = name[at];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            return false;
        }
    }
    (void)rsd_strcopy(user_name, RSD_NAME_MAX, name);
    user_set = true;
    return true;
}

void rsd_pkg_reset(void)
{
    pkg_count = 0U;
}

bool rsd_pkg_add(const char *name, const char *version, const char *root,
                  bool signed_ok)
{
    if (name == NULL || pkg_count >= RSD_PKGS) {
        return false;
    }
    (void)rsd_strcopy(pkgs[pkg_count].name, RSD_NAME_MAX, name);
    (void)rsd_strcopy(pkgs[pkg_count].version, 12U,
                       version != NULL ? version : "-");
    (void)rsd_strcopy(pkgs[pkg_count].root, 12U,
                       root != NULL ? root : "-");
    pkgs[pkg_count].signed_ok = signed_ok;
    ++pkg_count;
    return true;
}

uint32_t rsd_pkg_count(void)
{
    return pkg_count;
}

const struct rsd_pkg *rsd_pkg(uint32_t at)
{
    return at < pkg_count ? &pkgs[at] : (const struct rsd_pkg *)0;
}

/* ------------------------------------------------------------- dmesg */

static char dmesg[RSD_DMESG][RSD_DMESG_COLS];
static uint32_t dmesg_count;

void rsd_dmesg_reset(void)
{
    dmesg_count = 0U;
}

void rsd_dmesg_add(const char *line)
{
    if (dmesg_count >= RSD_DMESG) {
        /* The ring is full. Drop the oldest line rather than the
         * newest: the end of the boot is the part you are reading it
         * for. */
        for (uint32_t i = 1U; i < RSD_DMESG; ++i) {
            (void)rsd_strcopy(dmesg[i - 1U], RSD_DMESG_COLS, dmesg[i]);
        }
        dmesg_count = RSD_DMESG - 1U;
    }
    (void)rsd_strcopy(dmesg[dmesg_count], RSD_DMESG_COLS, line);
    ++dmesg_count;
}

uint32_t rsd_dmesg_count(void)
{
    return dmesg_count;
}

const char *rsd_dmesg_line(uint32_t at)
{
    return at < dmesg_count ? dmesg[at] : "";
}
