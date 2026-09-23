/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored MINIX 3 audio drivers: ioctl
 * request encoding, as MINIX 3's <sys/ioccom.h> (from NetBSD) defines it.
 * The drivers only compare requests against the codes <sys/ioc_sound.h>
 * builds from these macros; the glue passes the same codes.
 */
#ifndef OPENRFS_MINIX_IOCTL_H
#define OPENRFS_MINIX_IOCTL_H

#define IOCPARM_MASK 0xfff
#define IOCPARM_SHIFT 16
#define IOCGROUP_SHIFT 8
#define IOC_VOID (unsigned long)0x20000000
#define IOC_OUT (unsigned long)0x40000000
#define IOC_IN (unsigned long)0x80000000
#define IOC_INOUT (IOC_IN | IOC_OUT)

#define _IOC(inout, group, num, len) \
    ((inout) | (((len) & IOCPARM_MASK) << IOCPARM_SHIFT) | \
    ((group) << IOCGROUP_SHIFT) | (num))
#define _IO(g, n) _IOC(IOC_VOID, (g), (n), 0)
#define _IOR(g, n, t) _IOC(IOC_OUT, (g), (n), sizeof(t))
#define _IOW(g, n, t) _IOC(IOC_IN, (g), (n), sizeof(t))
#define _IOWR(g, n, t) _IOC(IOC_INOUT, (g), (n), sizeof(t))

#endif
