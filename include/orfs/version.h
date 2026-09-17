/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ORFS_VERSION_H
#define ORFS_VERSION_H

/*
 * The name and the release, in one place.
 *
 * They were in two. The console's banner said 0.1 and the installer's
 * title bar said 2.4, on the same machine, and both were right about
 * what somebody had typed into them. A number that appears twice is a
 * number that will disagree with itself.
 */
/* ORFS_SYSTEM, not ORFS_NAME: sys.h has had an ORFS_NAME since the
 * first commit and it is the length of a name field, not a name. */
#define ORFS_SYSTEM "OpenRFS"
#define ORFS_RELEASE "2.4"
#define ORFS_KERNEL "GENERIC"
#define ORFS_BUILD "#115"

/* "OpenRFS 2.4 (GENERIC) #115" - pasted together at compile time, so
 * there is no sprintf on a machine that has no printf. */
#define ORFS_BANNER \
    ORFS_SYSTEM " " ORFS_RELEASE " (" ORFS_KERNEL ") " ORFS_BUILD

#endif /* ORFS_VERSION_H */
