/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ORFS_VERSION_H
#define ORFS_VERSION_H

/*
 * Keep the name and release in one place so the console and installer agree.
 */
/* ORFS_SYSTEM, not ORFS_NAME: sys.h has had an ORFS_NAME since the
 * first commit and it is the length of a name field, not a name. */
#define ORFS_SYSTEM "OpenRFS"
#define ORFS_RELEASE "2.5 beta"
#define ORFS_KERNEL "GENERIC"
#define ORFS_BUILD "#115"

/* "OpenRFS 2.5 beta (GENERIC) #115" - pasted together at compile time, so
 * there is no sprintf on a machine that has no printf. */
#define ORFS_BANNER \
    ORFS_SYSTEM " " ORFS_RELEASE " (" ORFS_KERNEL ") " ORFS_BUILD

#endif /* ORFS_VERSION_H */
