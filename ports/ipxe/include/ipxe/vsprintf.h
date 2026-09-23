/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * iPXE's signed-size printf variants (include/ipxe/vsprintf.h), implemented
 * in ports/ipxe/libc.c as upstream's core/vsprintf.c does: a negative buffer
 * size is treated as zero.
 */
#ifndef OPENRFS_IPXE_VSPRINTF_H
#define OPENRFS_IPXE_VSPRINTF_H

#include <stdarg.h>
#include <stddef.h>

int vssnprintf(char *buffer, ssize_t size, const char *format, va_list args);
int ssnprintf(char *buffer, ssize_t size, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#endif
