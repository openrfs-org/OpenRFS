/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The compiler's freestanding stdint.h plus the legacy integer names iPXE's
 * include/stdint.h provides to its older Etherboot-derived drivers.
 */
#ifndef OPENRFS_IPXE_STDINT_H
#define OPENRFS_IPXE_STDINT_H

#include_next <stdint.h>

typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;

#endif
