/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_PACKAGES_H
#define TRAIT_PACKAGES_H

#include <stdbool.h>
#include <stdint.h>

#include <trait/surface.h>
#include <trait/window.h>

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

#define TRAIT_PACKAGES_MAX 12U
#define TRAIT_PACKAGES_NAME_BYTES 24U
#define TRAIT_PACKAGES_TEXT_BYTES 64U

enum trait_package_mark {
    TRAIT_PACKAGE_NONE = 0,
    TRAIT_PACKAGE_INSTALL,
    TRAIT_PACKAGE_REMOVE
};

struct trait_package {
    char name[TRAIT_PACKAGES_NAME_BYTES];
    char summary[TRAIT_PACKAGES_TEXT_BYTES];
    char menu_name[TRAIT_PACKAGES_NAME_BYTES];  /* "" = no menu entry */
    bool installed;
    enum trait_package_mark mark;
};

void trait_packages_reset(void);
bool trait_packages_add(const char *name, const char *summary,
    const char *menu_name, bool installed);
uint32_t trait_packages_count(void);
uint32_t trait_packages_installed_count(void);
const struct trait_package *trait_packages_at(uint32_t index);

void trait_packages_mark(uint32_t index, enum trait_package_mark mark);
uint32_t trait_packages_marked(void);
/* Carries out every mark and clears them.  Returns how many changed. */
uint32_t trait_packages_apply(void);
bool trait_packages_installed(const char *name);

void trait_packages_select(uint32_t index);
uint32_t trait_packages_selected(void);

/* Where Apply is, so a press can find it.  A button whose bounds only
 * the drawing code knows is a button nothing can hit. */
bool trait_packages_apply_bounds(const struct trait_window *window,
    struct trait_rect *out);

void trait_packages_draw(struct trait_surface *surface,
    const struct trait_window *window);

bool trait_packages_self_test(void);

#endif /* TRAIT_PACKAGES_H */
