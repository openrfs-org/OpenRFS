/* SPDX-License-Identifier: GPL-3.0-only */
/* The compiler's stddef.h plus iPXE's container_of (include/stddef.h). */
#ifndef OPENRFS_IPXE_STDDEF_H
#define OPENRFS_IPXE_STDDEF_H

#include_next <stddef.h>

#define container_of(ptr, type, field) ({ \
        typeof(ptr) __ptr = (ptr); \
        (type *)((void *)__ptr - offsetof(type, field)); })

#endif
