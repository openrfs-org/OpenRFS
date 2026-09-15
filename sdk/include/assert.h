/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_ASSERT_H
#define TRAIT_ASSERT_H

void __trait_assert(const char *expression, const char *file, int line);
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : \
    __trait_assert(#expression, __FILE__, __LINE__))
#endif

#endif
