/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_IPXE_UMALLOC_H
#define RSD_IPXE_UMALLOC_H

#include <ipxe/malloc.h>

#define umalloc(size) malloc_phys((size), 16)
#define ufree(pointer) free(pointer)

#endif
