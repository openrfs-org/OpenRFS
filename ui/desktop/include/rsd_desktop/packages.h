/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_PACKAGES_H
#define RSD_PACKAGES_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd_desktop/surface.h>
#include <rsd_desktop/window.h>

/*
 * Synaptic, which is Debian's package manager.
 *
 * Its window is a toolbar, a list of packages with a state box against
 * each, and a detail pane under it.  A package is marked for install or
 * removal and NOTHING HAPPENS until Apply; that two-step is the whole
 * shape of the program and the reason it is not just a list of switches.
 *
 * APPLY REALLY CHANGES THE DESKTOP.  What is installed decides what is in
 * the applications menu, so installing a package puts it there and
 * removing one takes it away.  A package manager whose Apply changed
 * nothing would be a shop window.
 */

#define RSD_PACKAGES_MAX 12U
#define RSD_PACKAGES_NAME_BYTES 24U
#define RSD_PACKAGES_TEXT_BYTES 64U

enum rsd_package_mark {
    RSD_PACKAGE_NONE = 0,
    RSD_PACKAGE_INSTALL,
    RSD_PACKAGE_REMOVE
};

struct rsd_package {
    char name[RSD_PACKAGES_NAME_BYTES];
    char summary[RSD_PACKAGES_TEXT_BYTES];
    char menu_name[RSD_PACKAGES_NAME_BYTES];  /* "" = no menu entry */
    bool installed;
    enum rsd_package_mark mark;
};

void rsd_packages_reset(void);
bool rsd_packages_add(const char *name, const char *summary,
    const char *menu_name, bool installed);
uint32_t rsd_packages_count(void);
uint32_t rsd_packages_installed_count(void);
const struct rsd_package *rsd_packages_at(uint32_t index);

void rsd_packages_mark(uint32_t index, enum rsd_package_mark mark);
uint32_t rsd_packages_marked(void);
/* Carries out every mark and clears them.  Returns how many changed. */
uint32_t rsd_packages_apply(void);
bool rsd_packages_installed(const char *name);

void rsd_packages_select(uint32_t index);
uint32_t rsd_packages_selected(void);

/* Where Apply is, so a press can find it.  A button whose bounds only
 * the drawing code knows is a button nothing can hit. */
bool rsd_packages_apply_bounds(const struct rsd_window *window,
    struct rsd_rect *out);

void rsd_packages_draw(struct rsd_surface *surface,
    const struct rsd_window *window);

bool rsd_packages_self_test(void);

#endif /* RSD_PACKAGES_H */
