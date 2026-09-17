/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DE_PACKAGES_H
#define OPENRFS_DE_PACKAGES_H

#include <stdbool.h>
#include <stdint.h>

#include <openrfs/de/surface.h>
#include <openrfs/de/window.h>

/*
 * The OpenRFS desktop environment package manager.
 *
 * Its window is a toolbar, a list of OpenRFS packages with a state box against
 * each, and a detail pane under it.  A package is marked for install or
 * removal and NOTHING HAPPENS until Apply; that two-step is the whole
 * shape of the program and the reason it is not just a list of switches.
 *
 * APPLY REALLY CHANGES THE DESKTOP.  What is installed decides what is in
 * the applications menu, so installing a package puts it there and
 * removing one takes it away.  A package manager whose Apply changed
 * nothing would be a shop window.
 */

#define OPENRFS_PACKAGES_MAX 12U
#define OPENRFS_PACKAGES_NAME_BYTES 24U
#define OPENRFS_PACKAGES_TEXT_BYTES 64U

enum openrfs_package_mark {
    OPENRFS_PACKAGE_NONE = 0,
    OPENRFS_PACKAGE_INSTALL,
    OPENRFS_PACKAGE_REMOVE
};

struct openrfs_package {
    char name[OPENRFS_PACKAGES_NAME_BYTES];
    char summary[OPENRFS_PACKAGES_TEXT_BYTES];
    char menu_name[OPENRFS_PACKAGES_NAME_BYTES];  /* "" = no menu entry */
    bool installed;
    enum openrfs_package_mark mark;
};

void openrfs_packages_reset(void);
bool openrfs_packages_add(const char *name, const char *summary,
    const char *menu_name, bool installed);
uint32_t openrfs_packages_count(void);
const struct openrfs_package *openrfs_packages_at(uint32_t index);

void openrfs_packages_mark(uint32_t index, enum openrfs_package_mark mark);
uint32_t openrfs_packages_marked(void);
/* Carries out every mark and clears them.  Returns how many changed. */
uint32_t openrfs_packages_apply(void);
bool openrfs_packages_installed(const char *name);

void openrfs_packages_select(uint32_t index);
uint32_t openrfs_packages_selected(void);

void openrfs_packages_draw(struct openrfs_surface *surface,
    const struct openrfs_window *window);

bool openrfs_packages_self_test(void);

#endif /* OPENRFS_DE_PACKAGES_H */
