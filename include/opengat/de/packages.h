/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_DE_PACKAGES_H
#define OPENGAT_DE_PACKAGES_H

#include <stdbool.h>
#include <stdint.h>

#include <opengat/de/surface.h>
#include <opengat/de/window.h>

/*
 * The OpenGAT desktop environment package manager.
 *
 * Its window is a toolbar, a list of OpenGAT packages with a state box against
 * each, and a detail pane under it.  A package is marked for install or
 * removal and NOTHING HAPPENS until Apply; that two-step is the whole
 * shape of the program and the reason it is not just a list of switches.
 *
 * APPLY REALLY CHANGES THE DESKTOP.  What is installed decides what is in
 * the applications menu, so installing a package puts it there and
 * removing one takes it away.  A package manager whose Apply changed
 * nothing would be a shop window.
 */

#define OPENGAT_PACKAGES_MAX 12U
#define OPENGAT_PACKAGES_NAME_BYTES 24U
#define OPENGAT_PACKAGES_TEXT_BYTES 64U

enum opengat_package_mark {
    OPENGAT_PACKAGE_NONE = 0,
    OPENGAT_PACKAGE_INSTALL,
    OPENGAT_PACKAGE_REMOVE
};

struct opengat_package {
    char name[OPENGAT_PACKAGES_NAME_BYTES];
    char summary[OPENGAT_PACKAGES_TEXT_BYTES];
    char menu_name[OPENGAT_PACKAGES_NAME_BYTES];  /* "" = no menu entry */
    bool installed;
    enum opengat_package_mark mark;
};

void opengat_packages_reset(void);
bool opengat_packages_add(const char *name, const char *summary,
    const char *menu_name, bool installed);
uint32_t opengat_packages_count(void);
const struct opengat_package *opengat_packages_at(uint32_t index);

void opengat_packages_mark(uint32_t index, enum opengat_package_mark mark);
uint32_t opengat_packages_marked(void);
/* Carries out every mark and clears them.  Returns how many changed. */
uint32_t opengat_packages_apply(void);
bool opengat_packages_installed(const char *name);

void opengat_packages_select(uint32_t index);
uint32_t opengat_packages_selected(void);

void opengat_packages_draw(struct opengat_surface *surface,
    const struct opengat_window *window);

bool opengat_packages_self_test(void);

#endif /* OPENGAT_DE_PACKAGES_H */
