/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DRIVER_TESTS_H
#define OPENRFS_DRIVER_TESTS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * The upstream driver suite's in-guest plans. The QEMU runner picks one per
 * boot with openrfs.drvtest=<plan> and passes the plan's parameters as
 * further openrfs.drv*= options; see tools/run_driver_tests.py.
 *
 * Returns true when every step of the plan passed; otherwise *reason names
 * the first step that did not.
 */
bool driver_tests_run(const char *command_line, size_t length,
    const char **reason);

#endif
