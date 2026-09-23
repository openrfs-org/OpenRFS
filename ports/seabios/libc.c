/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * String and formatting routines for the SeaBIOS environment, with the
 * names and semantics SeaBIOS's string.c and output.c give them. They are
 * local to the layer's object, so they never meet the kernel's own.
 */
#include <stdarg.h>

#include "string.h"
#include "types.h"
#include "hw/pci.h"
#include "hw/pcidevice.h"

#undef memcpy

void *memset(void *s, int c, size_t n)
{
    u8 *bytes = s;

    while (n--)
        *bytes++ = (u8)c;
    return s;
}

void memset_fl(void *ptr, u8 val, size_t size)
{
    memset(ptr, val, size);
}

void memset16_fl(void *ptr, u16 val, size_t size)
{
    u16 *words = ptr;

    size /= 2;
    while (size--)
        *words++ = val;
}

void *memcpy(void *d1, const void *s1, size_t len)
{
    u8 *destination = d1;
    const u8 *source = s1;

    while (len--)
        *destination++ = *source++;
    return d1;
}

void memcpy_fl(void *d_fl, const void *s_fl, size_t len)
{
    memcpy(d_fl, s_fl, len);
}

void *memmove(void *d, const void *s, size_t len)
{
    u8 *destination = d;
    const u8 *source = s;

    if (destination == source || len == 0)
        return d;
    if (destination < source) {
        while (len--)
            *destination++ = *source++;
    } else {
        destination += len;
        source += len;
        while (len--)
            *--destination = *--source;
    }
    return d;
}

/* Copy to or from device memory in 32-bit units, as SeaBIOS does. */
void iomemcpy(void *d, const void *s, u32 len)
{
    volatile u32 *destination = d;
    const volatile u32 *source = s;

    len /= 4;
    while (len--)
        *destination++ = *source++;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const u8 *left = s1;
    const u8 *right = s2;

    while (n--) {
        if (*left != *right)
            return *left < *right ? -1 : 1;
        left++;
        right++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t length = 0;

    while (s[length])
        length++;
    return length;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (u8)*s1 - (u8)*s2;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return NULL;
    }
}

/* Copy at most len-1 characters and always terminate (string.c). */
char *strtcpy(char *dest, const char *src, size_t len)
{
    char *d = dest;

    if (!len)
        return dest;
    while (--len && *src)
        *d++ = *src++;
    *d = '\0';
    return dest;
}

/* Strip control characters and blanks from both ends (string.c). */
char *nullTrailingSpace(char *buf)
{
    size_t len = strlen(buf);

    while (len && (u8)buf[len - 1] <= ' ')
        buf[--len] = '\0';
    while (*buf && (u8)*buf <= ' ')
        buf++;
    return buf;
}

u8 checksum(void *buf, u32 len)
{
    const u8 *bytes = buf;
    u8 sum = 0;

    while (len--)
        sum += *bytes++;
    return sum;
}

/****************************************************************
 * Formatting
 ****************************************************************/

struct out {
    char *buffer;
    size_t size;
    size_t used;
};

static void put(struct out *out, char c)
{
    if (out->used + 1 < out->size)
        out->buffer[out->used] = c;
    out->used++;
}

static void put_number(struct out *out, u64 value, unsigned int base,
                       int upper, int width, char pad, int negative)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char text[24];
    int count = 0;

    do {
        text[count++] = digits[value % base];
        value /= base;
    } while (value && count < (int)sizeof(text));
    if (negative && pad == '0') {
        put(out, '-');
        width--;
    }
    while (width > count + (negative && pad != '0')) {
        put(out, pad);
        width--;
    }
    if (negative && pad != '0')
        put(out, '-');
    while (count)
        put(out, text[--count]);
}

int openrfs_seabios_vsnprintf(char *buffer, size_t size, const char *fmt,
                              va_list args);

int openrfs_seabios_vsnprintf(char *buffer, size_t size, const char *fmt,
                              va_list args)
{
    struct out out = { buffer, size, 0 };

    for (; *fmt; fmt++) {
        char pad = ' ';
        int width = 0;
        int longs = 0;
        int left = 0;

        if (*fmt != '%') {
            put(&out, *fmt);
            continue;
        }
        fmt++;
        if (*fmt == '-') {
            left = 1;
            fmt++;
        }
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') {
            longs++;
            fmt++;
        }
        if (*fmt == 'z') {
            longs = 2;
            fmt++;
        }
        switch (*fmt) {
        case 'd':
        case 'i': {
            s64 value = longs >= 2 ? va_arg(args, s64) : va_arg(args, int);
            put_number(&out, value < 0 ? (u64)-value : (u64)value, 10, 0,
                       width, pad, value < 0);
            break;
        }
        case 'u':
            put_number(&out, longs >= 2 ? va_arg(args, u64)
                       : va_arg(args, unsigned int), 10, 0, width, pad, 0);
            break;
        case 'x':
        case 'X':
            put_number(&out, longs >= 2 ? va_arg(args, u64)
                       : va_arg(args, unsigned int), 16, *fmt == 'X', width,
                       pad, 0);
            break;
        case 'p':
            if (fmt[1] == 'P') {
                /* SeaBIOS: the location of a struct pci_device. */
                struct pci_device *pci = va_arg(args, struct pci_device *);
                fmt++;
                if (!pci) {
                    put(&out, '?');
                    break;
                }
                put_number(&out, pci_bdf_to_bus(pci->bdf), 16, 0, 2, '0', 0);
                put(&out, ':');
                put_number(&out, pci_bdf_to_dev(pci->bdf), 16, 0, 2, '0', 0);
                put(&out, '.');
                put_number(&out, pci_bdf_to_fn(pci->bdf), 16, 0, 1, '0', 0);
                break;
            }
            put(&out, '0');
            put(&out, 'x');
            put_number(&out, (openrfs_seabios_uintptr)va_arg(args, void *),
                       16, 0, width, pad, 0);
            break;
        case 's': {
            const char *text = va_arg(args, const char *);
            int length;

            if (!text)
                text = "(null)";
            length = (int)strlen(text);
            while (!left && width-- > length)
                put(&out, ' ');
            while (*text)
                put(&out, *text++);
            while (left && width-- > length)
                put(&out, ' ');
            break;
        }
        case 'c':
            put(&out, (char)va_arg(args, int));
            break;
        case '%':
            put(&out, '%');
            break;
        case '\0':
            fmt--;
            break;
        default:
            put(&out, '%');
            put(&out, *fmt);
            break;
        }
    }
    if (out.size)
        out.buffer[out.used < out.size ? out.used : out.size - 1] = '\0';
    return (int)out.used;
}
