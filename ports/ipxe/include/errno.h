/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Error numbers for iPXE drivers. iPXE encodes a unique identifier in every
 * error value for diagnostics; drivers only ever return, compare and
 * negate them, so plain positive POSIX numbers preserve every behaviour.
 */
#ifndef OPENRFS_IPXE_ERRNO_H
#define OPENRFS_IPXE_ERRNO_H

#define ENOENT 2
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define ENODEV 19
#define EINVAL 22
#define ENFILE 23
#define ENOTTY 25
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EPIPE 32
#define ERANGE 34
#define ENOSYS 38
#define ENODATA 61
#define ETIME 62
#define EPROTO 71
#define EOVERFLOW 75
#define EBADMSG 74
#define EMSGSIZE 90
#define ENOTSUP 95
#define EOPNOTSUPP ENOTSUP
#define EADDRINUSE 98
#define EADDRNOTAVAIL 99
#define ENETDOWN 100
#define ENETUNREACH 101
#define ECONNABORTED 103
#define ECONNRESET 104
#define ENOBUFS 105
#define ENOTCONN 107
#define ETIMEDOUT 110
#define ECONNREFUSED 111
#define EHOSTUNREACH 113
#define EALREADY 114
#define EINPROGRESS 115
#define ECANCELED 125
#define ENOTRECOVERABLE 131
#define EUNIQ_01 0

extern int errno;
const char *strerror(int error);

#endif
