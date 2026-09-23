/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored MINIX 3 audio drivers: error codes.
 * <minix/drivers.h> defines _SYSTEM, under which MINIX's <errno.h> makes
 * every code negative; these are those values.
 */
#ifndef OPENRFS_MINIX_ERRNO_H
#define OPENRFS_MINIX_ERRNO_H

#define EPERM (-1)
#define EIO (-5)
#define EAGAIN (-35)
#define EBUSY (-16)
#define EINVAL (-22)
#define ENOTTY (-25)

#endif
