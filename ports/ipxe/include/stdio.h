/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Formatted output for iPXE drivers. printf() writes to the OpenRFS serial
 * console; the formatter is ports/ipxe/libc.c's bounded vsnprintf.
 */
#ifndef OPENRFS_IPXE_STDIO_H
#define OPENRFS_IPXE_STDIO_H

#include <stdarg.h>
#include <stddef.h>

int printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int snprintf(char *buffer, size_t size, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int sprintf(char *buffer, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#endif
