/* SPDX-License-Identifier: GPL-3.0-only */
/* OpenRFS environment for the vendored MINIX 3 audio drivers: printf. */
#ifndef OPENRFS_MINIX_STDIO_H
#define OPENRFS_MINIX_STDIO_H

int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
