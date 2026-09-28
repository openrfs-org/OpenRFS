/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_ASSERT_H
#define RSD_ASSERT_H

void __rsd_assert(const char *expression, const char *file, int line);
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : \
    __rsd_assert(#expression, __FILE__, __LINE__))
#endif

#endif
