/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored MINIX 3 audio drivers: basic types,
 * with the widths MINIX 3's i386 port gives them. phys_bytes stays 32-bit:
 * every buffer these drivers hand a device lies below 4 GiB (below 16 MiB
 * for the ISA card).
 */
#ifndef OPENRFS_MINIX_SYS_TYPES_H
#define OPENRFS_MINIX_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;
typedef int8_t i8_t;
typedef int16_t i16_t;
typedef int32_t i32_t;
typedef int64_t i64_t;

typedef uint32_t phys_bytes;
typedef uint32_t vir_bytes;
typedef long ssize_t;

/* IPC types that appear in <minix/audio_fw.h>'s sub_dev_t. */
typedef int endpoint_t;
typedef int32_t cp_grant_id_t;
typedef int32_t cdev_id_t;
typedef int32_t devminor_t;

#endif
