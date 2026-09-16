/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_ASSERT_H
#define OPENGAT_ASSERT_H

void __opengat_assert(const char *expression, const char *file, int line);
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : \
    __opengat_assert(#expression, __FILE__, __LINE__))
#endif

#endif
