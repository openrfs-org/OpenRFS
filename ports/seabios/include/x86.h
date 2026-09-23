/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: processor and I/O
 * access. The same operations SeaBIOS's x86.h provides, written for x86-64
 * (64-bit flags, no segment or GDT helpers, which no vendored driver uses).
 */
#ifndef OPENRFS_SEABIOS_X86_H
#define OPENRFS_SEABIOS_X86_H

#include "types.h"

#define F_CF (1<<0)
#define F_ZF (1<<6)
#define F_IF (1<<9)
#define F_ID (1<<21)

static inline void irq_disable(void)
{
    __asm__ __volatile__("cli" : : : "memory");
}

static inline void irq_enable(void)
{
    __asm__ __volatile__("sti" : : : "memory");
}

static inline unsigned long save_flags(void)
{
    unsigned long flags;

    __asm__ __volatile__("pushfq ; popq %0" : "=rm"(flags) : : "memory");
    return flags;
}

static inline void restore_flags(unsigned long flags)
{
    __asm__ __volatile__("pushq %0 ; popfq" : : "g"(flags) : "memory", "cc");
}

static inline void cpu_relax(void)
{
    __asm__ __volatile__("rep ; nop" : : : "memory");
}

static inline void nop(void)
{
    __asm__ __volatile__("nop");
}

static inline u64 rdtscll(void)
{
    u32 low;
    u32 high;

    __asm__ __volatile__("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}

static inline void __cpuid(u32 index, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
    __asm__("cpuid"
            : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
            : "0"(index), "2"(0));
}

static inline u32 __ffs(u32 word)
{
    return (u32)__builtin_ctz(word);
}

static inline u32 __fls(u32 word)
{
    return 31U - (u32)__builtin_clz(word);
}

/*
 * Port I/O is a compiler memory barrier here, which SeaBIOS's own x86.h does
 * not state. Its drivers poll a status port and then read results a device
 * has DMA'd into variables whose addresses reached the device only as
 * integers - lsi-scsi.c's status and message bytes, for instance. SeaBIOS's
 * whole-program build happens not to cache those values; compiled one file at
 * a time, GCC may keep the value from before the transfer. The clobber makes
 * every in/out a point where memory is re-read, which is what the drivers
 * assume and what the hardware does.
 */
static inline void outb(u8 value, u16 port)
{
    __asm__ __volatile__("outb %b0, %w1" : : "a"(value), "Nd"(port)
                         : "memory");
}

static inline void outw(u16 value, u16 port)
{
    __asm__ __volatile__("outw %w0, %w1" : : "a"(value), "Nd"(port)
                         : "memory");
}

static inline void outl(u32 value, u16 port)
{
    __asm__ __volatile__("outl %0, %w1" : : "a"(value), "Nd"(port)
                         : "memory");
}

static inline u8 inb(u16 port)
{
    u8 value;

    __asm__ __volatile__("inb %w1, %b0" : "=a"(value) : "Nd"(port)
                         : "memory");
    return value;
}

static inline u16 inw(u16 port)
{
    u16 value;

    __asm__ __volatile__("inw %w1, %w0" : "=a"(value) : "Nd"(port)
                         : "memory");
    return value;
}

static inline u32 inl(u16 port)
{
    u32 value;

    __asm__ __volatile__("inl %w1, %0" : "=a"(value) : "Nd"(port)
                         : "memory");
    return value;
}

static inline void insb(u16 port, u8 *data, u32 count)
{
    __asm__ __volatile__("rep insb (%%dx), %%es:(%%rdi)"
                         : "+c"(count), "+D"(data) : "d"(port) : "memory");
}

static inline void insw(u16 port, u16 *data, u32 count)
{
    __asm__ __volatile__("rep insw (%%dx), %%es:(%%rdi)"
                         : "+c"(count), "+D"(data) : "d"(port) : "memory");
}

static inline void insl(u16 port, u32 *data, u32 count)
{
    __asm__ __volatile__("rep insl (%%dx), %%es:(%%rdi)"
                         : "+c"(count), "+D"(data) : "d"(port) : "memory");
}

static inline void outsb(u16 port, u8 *data, u32 count)
{
    __asm__ __volatile__("rep outsb %%ds:(%%rsi), (%%dx)"
                         : "+c"(count), "+S"(data) : "d"(port) : "memory");
}

static inline void outsw(u16 port, u16 *data, u32 count)
{
    __asm__ __volatile__("rep outsw %%ds:(%%rsi), (%%dx)"
                         : "+c"(count), "+S"(data) : "d"(port) : "memory");
}

static inline void outsl(u16 port, u32 *data, u32 count)
{
    __asm__ __volatile__("rep outsl %%ds:(%%rsi), (%%dx)"
                         : "+c"(count), "+S"(data) : "d"(port) : "memory");
}

/* Descriptor bits vgafb.c names on a 16-bit-only path. */
#define GDT_CODE     (0x9bULL << 40)
#define GDT_DATA     (0x93ULL << 40)
#define GDT_B        (0x1ULL << 54)
#define GDT_G        (0x1ULL << 55)
#define GDT_BASE(v)  ((((u64)(v) & 0xff000000) << 32)           \
                      | (((u64)(v) & 0x00ffffff) << 16))
#define GDT_LIMIT(v) ((((u64)(v) & 0x000f0000) << 32)   \
                      | (((u64)(v) & 0x0000ffff) << 0))
#define GDT_GRANLIMIT(v) (GDT_G | GDT_LIMIT((v) >> 12))

/* x86 keeps loads and stores in order; the barriers stop the compiler. */
static inline void smp_rmb(void)
{
    barrier();
}

static inline void smp_wmb(void)
{
    barrier();
}

static inline void writel(void *addr, u32 val)
{
    barrier();
    *(volatile u32 *)addr = val;
}

static inline void writew(void *addr, u16 val)
{
    barrier();
    *(volatile u16 *)addr = val;
}

static inline void writeb(void *addr, u8 val)
{
    barrier();
    *(volatile u8 *)addr = val;
}

static inline u64 readq(const void *addr)
{
    u64 val = *(volatile const u64 *)addr;

    barrier();
    return val;
}

static inline u32 readl(const void *addr)
{
    u32 val = *(volatile const u32 *)addr;

    barrier();
    return val;
}

static inline u16 readw(const void *addr)
{
    u16 val = *(volatile const u16 *)addr;

    barrier();
    return val;
}

static inline u8 readb(const void *addr)
{
    u8 val = *(volatile const u8 *)addr;

    barrier();
    return val;
}

#endif
