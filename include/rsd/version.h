/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_VERSION_H
#define RSD_VERSION_H

/*
 * The name and the release, in one place.
 *
 * They were in two. The console's banner said 0.1 and the installer's
 * title bar said 2.4, on the same machine, and both were right about
 * what somebody had typed into them. A number that appears twice is a
 * number that will disagree with itself.
 *
 * The name is RSD now - Root Software Distribution - and the release is
 * 2.5, both of which the wallpaper says in as many words.
 */
/* RSD_SYSTEM, not RSD_NAME: sys.h has had an RSD_NAME since the
 * first commit and it is the length of a name field, not a name. */
#define RSD_SYSTEM "RSD"
#define RSD_RELEASE "2.5"
#define RSD_KERNEL "GENERIC"
#define RSD_BUILD "#115"

/* "RSD 2.5 (GENERIC) #115" - pasted together at compile time, so
 * there is no sprintf on a machine that has no printf. */
#define RSD_BANNER \
    RSD_SYSTEM " " RSD_RELEASE " (" RSD_KERNEL ") " RSD_BUILD

#endif /* RSD_VERSION_H */
