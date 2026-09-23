/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: basic types.
 *
 * SeaBIOS's own types.h (not vendored) targets a 32-bit flat or 16-bit
 * segmented build and defines size_t as u32. Here the drivers are compiled
 * for x86-64 in the equivalent of SeaBIOS's 32-bit flat mode (MODE16 and
 * MODESEGMENT both zero), with every object they hand to a device placed
 * below 4 GiB and identity-mapped, which is what their u32 casts of
 * pointers assume.
 */
#ifndef OPENRFS_SEABIOS_TYPES_H
#define OPENRFS_SEABIOS_TYPES_H

#ifndef MODE16
#define MODE16 0
#endif
#ifndef MODESEGMENT
#define MODESEGMENT 0
#endif

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned long long u64;
typedef signed long long s64;
typedef __SIZE_TYPE__ size_t;
typedef __UINTPTR_TYPE__ openrfs_seabios_uintptr;

union u64_u32_u {
    struct { u32 lo, hi; };
    u64 val;
};

struct segoff_s {
    union {
        struct {
            u16 offset;
            u16 seg;
        };
        u32 segoff;
    };
};

#define __VISIBLE
#define UNIQSEC __FILE__ "." __stringify(__LINE__)
#define __noreturn __attribute__((noreturn))

/* Every mode annotation collapses to ordinary code and data. */
#define VISIBLE16
#define VISIBLE32FLAT
#define VISIBLE32INIT
#define VISIBLE32SEG
#define VAR16
#define VAR32SEG
#define VARLOW
#define VARFSEG
#define VARFSEGFIXED(addr)
#define VARVERIFY32INIT
#define ASM16(code)
#define ASM32FLAT(code)
#define ASSERT16() do { } while (0)
#define ASSERT32SEG() do { } while (0)
#define ASSERT32FLAT() do { } while (0)

#ifndef offsetof
#define offsetof(TYPE, MEMBER) __builtin_offsetof (TYPE, MEMBER)
#endif
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define FIELD_SIZEOF(t, f) (sizeof(((t*)0)->f))
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_CLOSEST(x, divisor)({                 \
            typeof(divisor) __divisor = divisor;        \
            (((x) + ((__divisor) / 2)) / (__divisor));  \
        })
#define ALIGN(x,a)              __ALIGN_MASK(x,(typeof(x))(a)-1)
#define __ALIGN_MASK(x,mask)    (((x)+(mask))&~(mask))
#define ALIGN_DOWN(x,a)         ((x) & ~((typeof(x))(a)-1))
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#define container_of_or_null(ptr, type, member) ({              \
        const typeof( ((type *)0)->member ) *___mptr = (ptr);   \
        ___mptr ? container_of(___mptr, type, member) : NULL; })

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#ifndef NULL
#define NULL ((void*)0)
#endif

#define __weak __attribute__((weak))
#define __section(S) __attribute__((section(S)))
#define PACKED __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define barrier() __asm__ __volatile__("": : :"memory")
#define noinline __attribute__((noinline))
#define __always_inline inline __attribute__((always_inline))
#define __malloc __attribute__((__malloc__))
#define __attribute_const __attribute__((__const__))

#define __stringify_1(x)        #x
#define __stringify(x)          __stringify_1(x)

#endif
