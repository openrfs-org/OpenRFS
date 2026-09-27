/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_FCNTL_H
#define OPENRFS_FCNTL_H
#define O_RDONLY 0x0001
#define O_WRONLY 0x0002
#define O_RDWR 0x0003
#define O_CREAT 0x0100
#define O_TRUNC 0x0200
#define O_APPEND 0x0400
#define O_EXCL 0x0800
#define O_CLOEXEC 0x1000
#define O_CLOFORK 0x2000

#define FD_CLOEXEC 0x01
#define FD_CLOFORK 0x02
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_DUPFD_CLOEXEC 5
#define F_DUPFD_CLOFORK 6
int open(const char *path, int flags, ...);
int fcntl(int descriptor, int command, ...);
#endif
