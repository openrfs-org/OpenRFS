/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_IPXE_STRINGS_H
#define OPENRFS_IPXE_STRINGS_H

#include <string.h>

static inline int ffs(int value)
{
    return __builtin_ffs(value);
}

static inline int openrfs_ipxe_fls(unsigned long long value)
{
    return value == 0U ? 0 : 64 - __builtin_clzll(value);
}

#define fls(value) openrfs_ipxe_fls((unsigned long long)(value))
#define flsl(value) fls(value)
#define flsll(value) fls(value)

#endif
