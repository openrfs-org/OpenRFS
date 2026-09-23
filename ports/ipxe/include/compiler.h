/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The environment pinned iPXE driver sources are compiled in. Every iPXE
 * translation unit is built with this header force-included, exactly as
 * iPXE's own build force-includes include/compiler.h. Only the parts of that
 * interface the vendored drivers use are provided; the names and meanings
 * follow iPXE's include/compiler.h (GPL-2.0-or-later OR UBDL).
 *
 * Debug output: iPXE compiles DBG*() to nothing unless a build asks for a
 * debug level, and OpenRFS never does. The arguments are still parsed inside
 * an if (0) so a driver's debug-only variables stay referenced.
 */
#ifndef OPENRFS_IPXE_COMPILER_H
#define OPENRFS_IPXE_COMPILER_H

#define FILE_LICENCE(licence)
#define FILE_SECBOOT(status)
#define REQUIRE_OBJECT(object)
#define REQUIRING_SYMBOL(symbol)
#define PROVIDE_REQUIRING_SYMBOL()
#define PROVIDE_SYMBOL(symbol)
#define EXPORT_SYMBOL(symbol)

#define __unused __attribute__((unused))
#define __used __attribute__((used))
#define __maybe_unused __attribute__((unused))
#define __always_inline __attribute__((always_inline))
#define __malloc __attribute__((malloc))
#define __attribute_const__ __attribute__((const))
#define __nonnull __attribute__((nonnull))
#define __weak __attribute__((weak))
#define __asmcall
#define __unlikely(x) __builtin_expect(!!(x), 0)
#define __likely(x) __builtin_expect(!!(x), 1)
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define barrier() __asm__ __volatile__("" : : : "memory")

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#define DBGLVL_LOG 1
#define DBGLVL_EXTRA 2
#define DBGLVL_PROFILE 4
#define DBGLVL_IO 8
#define DBGLVL 0
#define DBG_LOG 0
#define DBG_EXTRA 0
#define DBG_PROFILE 0
#define DBG_IO 0
#define DBG_ENABLE(level) do { } while (0)
#define DBG_DISABLE(level) do { } while (0)
#define DBG_ENABLE_IF(level) do { } while (0)
#define DBG_DISABLE_IF(level) do { } while (0)

static inline void openrfs_ipxe_debug_discard(const char *format, ...)
{
    (void)format;
}

static inline void openrfs_ipxe_debug_dump_discard(const void *data,
    unsigned long length)
{
    (void)data;
    (void)length;
}

#define OPENRFS_IPXE_DEBUG(...) do { \
        if (0) { openrfs_ipxe_debug_discard(__VA_ARGS__); } \
    } while (0)
#define OPENRFS_IPXE_DEBUG_OBJECT(object, ...) do { \
        if (0) { (void)(object); openrfs_ipxe_debug_discard(__VA_ARGS__); } \
    } while (0)
#define OPENRFS_IPXE_DEBUG_DUMP(data, length) do { \
        if (0) { openrfs_ipxe_debug_dump_discard((data), (length)); } \
    } while (0)
#define OPENRFS_IPXE_DEBUG_DUMP_OBJECT(object, data, length) do { \
        if (0) { (void)(object); \
            openrfs_ipxe_debug_dump_discard((data), (length)); } \
    } while (0)
#define OPENRFS_IPXE_DEBUG_DUMPA_OBJECT(object, address, data, length) do { \
        if (0) { (void)(object); (void)(address); \
            openrfs_ipxe_debug_dump_discard((data), (length)); } \
    } while (0)

/*
 * A build with IPXE_DEBUG=1 (-DOPENRFS_IPXE_DEBUG_OUTPUT) prints the drivers'
 * level-1 messages, DBG() and DBGC(), to the serial console, as an iPXE build
 * with DEBUG= set for those files would; the other levels stay silent. The
 * declaration carries no format attribute: iPXE's own uint64_t is unsigned
 * long long, so its "%llx" strings are right for iPXE and would be flagged
 * against the compiler's stdint.h.
 */
#ifdef OPENRFS_IPXE_DEBUG_OUTPUT
int printf(const char *format, ...);
#define DBG(...) do { printf(__VA_ARGS__); } while (0)
#define DBGC(object, ...) do { (void)(object); printf(__VA_ARGS__); } while (0)
#else
#define DBG(...) OPENRFS_IPXE_DEBUG(__VA_ARGS__)
#define DBGC(object, ...) OPENRFS_IPXE_DEBUG_OBJECT(object, __VA_ARGS__)
#endif
#define DBG2(...) OPENRFS_IPXE_DEBUG(__VA_ARGS__)
#define DBGP(...) OPENRFS_IPXE_DEBUG(__VA_ARGS__)
#define DBGIO(...) OPENRFS_IPXE_DEBUG(__VA_ARGS__)
#define DBGC2(object, ...) OPENRFS_IPXE_DEBUG_OBJECT(object, __VA_ARGS__)
#define DBGCP(object, ...) OPENRFS_IPXE_DEBUG_OBJECT(object, __VA_ARGS__)
#define DBGCIO(object, ...) OPENRFS_IPXE_DEBUG_OBJECT(object, __VA_ARGS__)
#define DBG_HD(data, length) OPENRFS_IPXE_DEBUG_DUMP(data, length)
#define DBG2_HD(data, length) OPENRFS_IPXE_DEBUG_DUMP(data, length)
#define DBGIO_HD(data, length) OPENRFS_IPXE_DEBUG_DUMP(data, length)
#define DBGC_HD(object, data, length) \
    OPENRFS_IPXE_DEBUG_DUMP_OBJECT(object, data, length)
#define DBGC2_HD(object, data, length) \
    OPENRFS_IPXE_DEBUG_DUMP_OBJECT(object, data, length)
#define DBGCP_HD(object, data, length) \
    OPENRFS_IPXE_DEBUG_DUMP_OBJECT(object, data, length)
#define DBGC_HDA(object, address, data, length) \
    OPENRFS_IPXE_DEBUG_DUMPA_OBJECT(object, address, data, length)
#define DBGC2_HDA(object, address, data, length) \
    OPENRFS_IPXE_DEBUG_DUMPA_OBJECT(object, address, data, length)
#define DBGCP_HDA(object, address, data, length) \
    OPENRFS_IPXE_DEBUG_DUMPA_OBJECT(object, address, data, length)

/* iPXE's error-inclusion hooks carry no meaning outside its build. */
#define ERRFILE 0

#endif
