/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: diagnostics.
 *
 * dprintf keeps SeaBIOS's level semantics; messages at or below
 * CONFIG_DEBUG_LEVEL reach the serial console prefixed "SeaBIOS: ". The
 * format engine understands SeaBIOS's %pP (a PCI device's location).
 */
#ifndef OPENRFS_SEABIOS_OUTPUT_H
#define OPENRFS_SEABIOS_OUTPUT_H

#include "config.h"
#include "types.h"

void panic(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2))) __noreturn;
void printf(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));
int snprintf(char *str, size_t size, const char *fmt, ...)
    __attribute__ ((format (printf, 3, 4)));
char * znprintf(size_t size, const char *fmt, ...)
    __attribute__ ((format (printf, 2, 3)));
void __dprintf(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));
void __warn_internalerror(int lineno, const char *fname);
void __warn_noalloc(int lineno, const char *fname);
void __warn_timeout(int lineno, const char *fname);
void hexdump(const void *d, int len);

#define dprintf(lvl, fmt, args...) do {                         \
        if (CONFIG_DEBUG_LEVEL && (lvl) <= CONFIG_DEBUG_LEVEL)  \
            __dprintf((fmt) , ##args );                         \
    } while (0)
#define debug_isr(lvl) do { } while (0)
#define warn_internalerror()                    \
    __warn_internalerror(__LINE__, __func__)
#define warn_noalloc()                          \
    __warn_noalloc(__LINE__, __func__)
#define warn_timeout()                          \
    __warn_timeout(__LINE__, __func__)

#endif
