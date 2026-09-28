/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_CONFIG_H
#define RSD_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The kernel configuration.
 *
 * A BSD kernel is built from a config file - a list of what to compile
 * in - and `config` is the program that reads it. This is that list,
 * held in memory, with two ways to edit it: a full-screen menu, and the
 * line-oriented prompt OpenBSD calls UKC.
 *
 * Some options cannot be turned off. Root is on nvme0 and formatted
 * FAT32, so removing either leaves a kernel that cannot find its own
 * filesystem, and the two rules the build verifies (W^X, no FP in the
 * kernel) are not switches.
 */

#define RSD_OPT_MAX 26U
#define RSD_CONF_GROUPS 6U

struct rsd_opt {
    const char *name;    /* SMP */
    const char *desc;    /* what it is */
    uint8_t group;
    const char *help;    /* one line, for the bottom of the screen */
    bool on;
    bool locked;         /* the menu will not move it */
};

const char *rsd_conf_group(uint32_t group);
uint32_t rsd_conf_count(void);
struct rsd_opt *rsd_conf_at(uint32_t index);
/* Options in one group, in order. Returns NULL past the end. */
struct rsd_opt *rsd_conf_in(uint32_t group, uint32_t nth);
uint32_t rsd_conf_group_count(uint32_t group);
uint32_t rsd_conf_group_on(uint32_t group);

void rsd_conf_reset(void);
struct rsd_opt *rsd_conf_find(const char *name);
/* False if there is no such option, or if it is locked. */
bool rsd_conf_set(const char *name, bool on);
bool rsd_conf_get(const char *name);
/* How many differ from the defaults, which is what "unsaved" means. */
uint32_t rsd_conf_changed(void);
void rsd_conf_commit(void);

#endif /* RSD_CONFIG_H */
