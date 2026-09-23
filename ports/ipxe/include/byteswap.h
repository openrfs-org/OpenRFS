/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Byte order conversion with iPXE's names (include/byteswap.h). x86-64 is
 * little-endian, so the little-endian conversions are identities.
 */
#ifndef OPENRFS_IPXE_BYTESWAP_H
#define OPENRFS_IPXE_BYTESWAP_H

#include <stdint.h>
#include <endian.h>

#define bswap_16(value) ((uint16_t)__builtin_bswap16((uint16_t)(value)))
#define bswap_32(value) ((uint32_t)__builtin_bswap32((uint32_t)(value)))
#define bswap_64(value) ((uint64_t)__builtin_bswap64((uint64_t)(value)))
#define __bswap_16(value) bswap_16(value)
#define __bswap_32(value) bswap_32(value)
#define __bswap_64(value) bswap_64(value)
#define __bswap_16s(pointer) (*(pointer) = bswap_16(*(pointer)))
#define __bswap_32s(pointer) (*(pointer) = bswap_32(*(pointer)))
#define __bswap_64s(pointer) (*(pointer) = bswap_64(*(pointer)))

#define cpu_to_le16(value) ((uint16_t)(value))
#define cpu_to_le32(value) ((uint32_t)(value))
#define cpu_to_le64(value) ((uint64_t)(value))
#define le16_to_cpu(value) ((uint16_t)(value))
#define le32_to_cpu(value) ((uint32_t)(value))
#define le64_to_cpu(value) ((uint64_t)(value))
#define cpu_to_be16(value) bswap_16(value)
#define cpu_to_be32(value) bswap_32(value)
#define cpu_to_be64(value) bswap_64(value)
#define be16_to_cpu(value) bswap_16(value)
#define be32_to_cpu(value) bswap_32(value)
#define be64_to_cpu(value) bswap_64(value)

#define cpu_to_le16s(pointer) do { } while (0)
#define cpu_to_le32s(pointer) do { } while (0)
#define cpu_to_le64s(pointer) do { } while (0)
#define le16_to_cpus(pointer) do { } while (0)
#define le32_to_cpus(pointer) do { } while (0)
#define le64_to_cpus(pointer) do { } while (0)
#define cpu_to_be16s(pointer) __bswap_16s(pointer)
#define cpu_to_be32s(pointer) __bswap_32s(pointer)
#define cpu_to_be64s(pointer) __bswap_64s(pointer)
#define be16_to_cpus(pointer) __bswap_16s(pointer)
#define be32_to_cpus(pointer) __bswap_32s(pointer)
#define be64_to_cpus(pointer) __bswap_64s(pointer)

#define htonll(value) cpu_to_be64(value)
#define ntohll(value) be64_to_cpu(value)
#define htonl(value) cpu_to_be32(value)
#define ntohl(value) be32_to_cpu(value)
#define htons(value) cpu_to_be16(value)
#define ntohs(value) be16_to_cpu(value)

#endif
