/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * iPXE compiles assertions out unless a build defines ASSERTING, and the
 * vendored drivers are built exactly that way. The condition is still parsed
 * so it cannot rot; it is never evaluated.
 */
#ifndef OPENRFS_IPXE_ASSERT_H
#define OPENRFS_IPXE_ASSERT_H

#define ASSERTING 0
#define assert(condition) do { \
        if (ASSERTING && !(condition)) { } \
    } while (0)
#define linker_assert(condition, error_symbol) \
    _Static_assert((condition), #error_symbol)
#define build_assert(condition) _Static_assert((condition), #condition)

#endif
