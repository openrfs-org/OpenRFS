/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_ASSERT_H
#define OPENRFS_ASSERT_H

void __openrfs_assert(const char *expression, const char *file, int line);
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : \
    __openrfs_assert(#expression, __FILE__, __LINE__))
#endif

#endif
