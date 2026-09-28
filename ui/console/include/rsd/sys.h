/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_SYS_H
#define RSD_SYS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * What the shell can tell you about the machine.
 *
 * The BOOT LEDGER is the part of this that is RSD's and nobody
 * else's. RSD declares its boot stages as typed records and checks
 * their dependency order before any of them runs. That record survives
 * into userland, so the console can account for its own boot: not "it
 * booted" but "these fourteen things were checked, in this order, and
 * here is the one that was skipped and why".
 *
 * Every other command here is deliberately ordinary. A console earns
 * trust by being unsurprising.
 */

#define RSD_LEDGER_MAX 20U
#define RSD_NAME_MAX 24U
#define RSD_TEXT 48U

enum rsd_stage_verdict {
    RSD_STAGE_VERIFIED = 0,
    RSD_STAGE_SKIPPED,          /* declared, not run, and it says why */
    RSD_STAGE_FAILED
};

struct rsd_stage {
    char name[RSD_NAME_MAX];
    char note[RSD_TEXT];
    enum rsd_stage_verdict verdict;
    uint32_t us;                 /* microseconds the stage took */
};

void rsd_sys_reset(void);
bool rsd_sys_add_stage(const char *name, const char *note,
                        enum rsd_stage_verdict verdict, uint32_t us);
uint32_t rsd_sys_stage_count(void);
const struct rsd_stage *rsd_sys_stage(uint32_t at);
uint32_t rsd_sys_verified_count(void);

/* ------------------------------------------------------------ the tree
 *
 * FAT32 with 8.3 names, because that is the filesystem RSD has. No
 * long names, no permissions, no symlinks. A directory listing that
 * showed rwxr-xr-x would be inventing a security model the system does
 * not implement.
 */

#define RSD_NODES 48U
#define RSD_FNAME 13U           /* 8 + '.' + 3 + NUL */

struct rsd_node {
    char name[RSD_FNAME];
    uint32_t parent;
    uint32_t bytes;
    bool dir;
    bool used;
};

uint32_t rsd_fs_root(void);
uint32_t rsd_fs_add(uint32_t parent, const char *name, bool dir,
                     uint32_t bytes);
uint32_t rsd_fs_count(void);
const struct rsd_node *rsd_fs_node(uint32_t at);
uint32_t rsd_fs_find(uint32_t parent, const char *name);
uint32_t rsd_fs_parent(uint32_t node);
void rsd_fs_path(uint32_t node, char *out, uint32_t capacity);
bool rsd_fs_remove(uint32_t node);

/* ---------------------------------------------------------- the account
 *
 * One record, matching RSD/LOGIN.DAT: a name and whether a password
 * has been set. The shell never holds the password or the digest.
 */
bool rsd_user_exists(void);
const char *rsd_user_name(void);
bool rsd_user_add(const char *name);

/* --------------------------------------------------------- the packages
 *
 * Ed25519 against a trust root the system already holds. The console can
 * say which root signed a package, and it says "unsigned" rather than
 * nothing when there is no signature.
 */
#define RSD_PKGS 8U

struct rsd_pkg {
    char name[RSD_NAME_MAX];
    char version[12];
    char root[12];               /* the trust root's short id, or "-" */
    bool signed_ok;
};

void rsd_pkg_reset(void);
bool rsd_pkg_add(const char *name, const char *version, const char *root,
                  bool signed_ok);
uint32_t rsd_pkg_count(void);
const struct rsd_pkg *rsd_pkg(uint32_t at);

/* ------------------------------------------------------------- dmesg */
/*
 * What the kernel said on the way up, kept so it can be said again.
 *
 * The messages scroll off a 25-row console before anybody has read
 * them, which is why every Unix since the seventies has kept a copy.
 * This one is a fixed ring: the buffer is the size it is, and what
 * falls out of the top is gone, because a console with no allocator
 * cannot grow one.
 */
/*
 * THE MESSAGE BUFFER, sized like one.
 *
 * It was 56 lines, which was exactly as much as the boot printed while
 * the boot was eleven lines of probe. A BSD kernel's msgbuf is 16 KB -
 * MSGBUFSIZE - which at eighty columns is two hundred and four lines,
 * and the point of it is that it holds the WHOLE boot after the boot
 * has scrolled off the console. A buffer that drops the copyright is a
 * buffer that cannot answer the question dmesg is asked.
 */
#define RSD_DMESG 204U
#define RSD_DMESG_COLS 80U

void rsd_dmesg_reset(void);
void rsd_dmesg_add(const char *line);
uint32_t rsd_dmesg_count(void);
const char *rsd_dmesg_line(uint32_t at);

#endif /* RSD_SYS_H */
