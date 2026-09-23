/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The small C library the vendored iPXE drivers call into: string handling, a
 * bounded formatter for their printf()s, strerror() for their diagnostics and
 * the random() some of them use for a fallback MAC address. Everything here
 * is freestanding and bounded by its explicit length arguments.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ipxe/string.h>
#include <ipxe/vsprintf.h>

#include <openrfs/ipxe_host.h>

int errno;

void *memchr(const void *source, int character, size_t length)
{
    const unsigned char *bytes = source;

    for (size_t index = 0U; index < length; ++index) {
        if (bytes[index] == (unsigned char)character) {
            return (void *)(bytes + index);
        }
    }
    return NULL;
}

size_t strlen(const char *text)
{
    size_t length = 0U;

    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

size_t strnlen(const char *text, size_t maximum)
{
    size_t length = 0U;

    while (length < maximum && text[length] != '\0') {
        ++length;
    }
    return length;
}

char *strcpy(char *destination, const char *source)
{
    size_t index = 0U;

    do {
        destination[index] = source[index];
    } while (source[index++] != '\0');
    return destination;
}

char *strncpy(char *destination, const char *source, size_t length)
{
    size_t index = 0U;

    for (; index < length && source[index] != '\0'; ++index) {
        destination[index] = source[index];
    }
    for (; index < length; ++index) {
        destination[index] = '\0';
    }
    return destination;
}

int strcmp(const char *left, const char *right)
{
    size_t index = 0U;

    while (left[index] != '\0' && left[index] == right[index]) {
        ++index;
    }
    return (int)(unsigned char)left[index] - (int)(unsigned char)right[index];
}

int strncmp(const char *left, const char *right, size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        if (left[index] != right[index] || left[index] == '\0') {
            return (int)(unsigned char)left[index] -
                (int)(unsigned char)right[index];
        }
    }
    return 0;
}

char *strchr(const char *text, int character)
{
    for (;; ++text) {
        if (*text == (char)character) {
            return (char *)text;
        }
        if (*text == '\0') {
            return NULL;
        }
    }
}

char *strrchr(const char *text, int character)
{
    const char *found = NULL;

    for (;; ++text) {
        if (*text == (char)character) {
            found = text;
        }
        if (*text == '\0') {
            return (char *)found;
        }
    }
}

char *strcat(char *destination, const char *source)
{
    strcpy(destination + strlen(destination), source);
    return destination;
}

/*
 * The value of one digit in any base up to 36, as upstream's core/string.c
 * computes it; anything that is not a digit yields a value of 36 or more.
 */
unsigned int digit_value(unsigned int character)
{
    if (character >= 'a') {
        return character - ('a' - 10);
    }
    if (character >= 'A') {
        return character - ('A' - 10);
    }
    if (character <= '9') {
        return character - '0';
    }
    return character;
}

unsigned long strtoul(const char *text, char **end, int base)
{
    unsigned long value = 0U;
    size_t index = 0U;

    while (text[index] == ' ') {
        ++index;
    }
    if ((base == 0 || base == 16) && text[index] == '0' &&
        (text[index + 1U] == 'x' || text[index + 1U] == 'X')) {
        index += 2U;
        base = 16;
    } else if (base == 0) {
        base = text[index] == '0' ? 8 : 10;
    }
    for (;; ++index) {
        const char character = text[index];
        int digit;

        if (character >= '0' && character <= '9') {
            digit = character - '0';
        } else if (character >= 'a' && character <= 'z') {
            digit = character - 'a' + 10;
        } else if (character >= 'A' && character <= 'Z') {
            digit = character - 'A' + 10;
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        value = value * (unsigned long)base + (unsigned long)digit;
    }
    if (end != NULL) {
        *end = (char *)(text + index);
    }
    return value;
}

static uint64_t random_state = UINT64_C(0x853C49E6748FEA9B);

long int random(void)
{
    /* A 64-bit LCG (Knuth MMIX constants); drivers want variety, not keys. */
    random_state = random_state * UINT64_C(6364136223846793005) +
        UINT64_C(1442695040888963407);
    return (long int)((random_state >> 33U) & UINT64_C(0x7FFFFFFF));
}

void srandom(unsigned int seed)
{
    random_state = (uint64_t)seed ^ UINT64_C(0x853C49E6748FEA9B);
}

struct format_output {
    char *buffer;
    size_t size;
    size_t length;
};

static void emit(struct format_output *output, char character)
{
    if (output->size != 0U && output->length + 1U < output->size) {
        output->buffer[output->length] = character;
    }
    ++output->length;
}

static void emit_number(struct format_output *output, unsigned long long value,
    unsigned int base, bool upper, bool negative, size_t width, bool zero_pad,
    bool left)
{
    char digits[24];
    size_t count = 0U;
    size_t total;

    do {
        const unsigned int digit = (unsigned int)(value % base);

        digits[count++] = (char)(digit < 10U ? '0' + digit :
            (upper ? 'A' : 'a') + digit - 10U);
        value /= base;
    } while (value != 0U && count < sizeof(digits));
    total = count + (negative ? 1U : 0U);
    if (!left && !zero_pad) {
        for (; total < width; ++total) {
            emit(output, ' ');
        }
    }
    if (negative) {
        emit(output, '-');
    }
    if (!left && zero_pad) {
        for (; total < width; ++total) {
            emit(output, '0');
        }
    }
    while (count > 0U) {
        emit(output, digits[--count]);
    }
    if (left) {
        for (; total < width; ++total) {
            emit(output, ' ');
        }
    }
}

int vsnprintf(char *buffer, size_t size, const char *format, va_list args)
{
    struct format_output output = { buffer, size, 0U };

    for (size_t index = 0U; format[index] != '\0'; ++index) {
        bool left = false;
        bool zero_pad = false;
        size_t width = 0U;
        size_t precision = SIZE_MAX;
        int length_modifier = 0;
        char conversion;

        if (format[index] != '%') {
            emit(&output, format[index]);
            continue;
        }
        ++index;
        for (;; ++index) {
            if (format[index] == '-') {
                left = true;
            } else if (format[index] == '0') {
                zero_pad = true;
            } else if (format[index] != '+' && format[index] != ' ' &&
                format[index] != '#') {
                break;
            }
        }
        if (format[index] == '*') {
            const int requested = va_arg(args, int);

            width = requested < 0 ? 0U : (size_t)requested;
            ++index;
        }
        while (format[index] >= '0' && format[index] <= '9') {
            width = width * 10U + (size_t)(format[index] - '0');
            ++index;
        }
        if (format[index] == '.') {
            precision = 0U;
            ++index;
            if (format[index] == '*') {
                const int requested = va_arg(args, int);

                precision = requested < 0 ? 0U : (size_t)requested;
                ++index;
            }
            while (format[index] >= '0' && format[index] <= '9') {
                precision = precision * 10U + (size_t)(format[index] - '0');
                ++index;
            }
        }
        while (format[index] == 'h' || format[index] == 'l' ||
            format[index] == 'z' || format[index] == 'j' ||
            format[index] == 't' || format[index] == 'L') {
            if (format[index] == 'l' || format[index] == 'z' ||
                format[index] == 'j' || format[index] == 't' ||
                format[index] == 'L') {
                ++length_modifier;
            }
            ++index;
        }
        conversion = format[index];
        if (conversion == '\0') {
            break;
        }
        switch (conversion) {
        case 'd':
        case 'i': {
            long long value = length_modifier >= 2 ?
                va_arg(args, long long) : length_modifier == 1 ?
                (long long)va_arg(args, long) : (long long)va_arg(args, int);
            const bool negative = value < 0;
            const unsigned long long magnitude = negative ?
                (unsigned long long)(-(value + 1)) + 1U :
                (unsigned long long)value;

            emit_number(&output, magnitude, 10U, false, negative, width,
                zero_pad, left);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            const unsigned long long value = length_modifier >= 2 ?
                va_arg(args, unsigned long long) : length_modifier == 1 ?
                (unsigned long long)va_arg(args, unsigned long) :
                (unsigned long long)va_arg(args, unsigned int);

            emit_number(&output, value, conversion == 'u' ? 10U :
                conversion == 'o' ? 8U : 16U, conversion == 'X', false, width,
                zero_pad, left);
            break;
        }
        case 'p':
            emit(&output, '0');
            emit(&output, 'x');
            emit_number(&output, (unsigned long long)(uintptr_t)
                va_arg(args, void *), 16U, false, false, 0U, false, false);
            break;
        case 'c':
            emit(&output, (char)va_arg(args, int));
            break;
        case 's': {
            const char *text = va_arg(args, const char *);
            size_t text_length;

            if (text == NULL) {
                text = "(null)";
            }
            text_length = strnlen(text, precision);
            if (!left) {
                for (size_t pad = text_length; pad < width; ++pad) {
                    emit(&output, ' ');
                }
            }
            for (size_t character = 0U; character < text_length;
                 ++character) {
                emit(&output, text[character]);
            }
            if (left) {
                for (size_t pad = text_length; pad < width; ++pad) {
                    emit(&output, ' ');
                }
            }
            break;
        }
        case '%':
            emit(&output, '%');
            break;
        default:
            emit(&output, '%');
            emit(&output, conversion);
            break;
        }
    }
    if (size != 0U) {
        buffer[output.length < size ? output.length : size - 1U] = '\0';
    }
    return (int)output.length;
}

int snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    int length;

    va_start(args, format);
    length = vsnprintf(buffer, size, format, args);
    va_end(args);
    return length;
}

/* As upstream's core/vsprintf.c: a negative size means no buffer. */
int vssnprintf(char *buffer, ssize_t size, const char *format, va_list args)
{
    if (size < 0) {
        size = 0;
    }
    return vsnprintf(buffer, (size_t)size, format, args);
}

int ssnprintf(char *buffer, ssize_t size, const char *format, ...)
{
    va_list args;
    int length;

    va_start(args, format);
    length = vssnprintf(buffer, size, format, args);
    va_end(args);
    return length;
}

int sprintf(char *buffer, const char *format, ...)
{
    va_list args;
    int length;

    /* Callers size their own buffers; the bound is a backstop only. */
    va_start(args, format);
    length = vsnprintf(buffer, 256U, format, args);
    va_end(args);
    return length;
}

int printf(const char *format, ...)
{
    char line[160];
    va_list args;
    int length;

    va_start(args, format);
    length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    ipxe_glue_console_write(line);
    return length;
}

const char *strerror(int error)
{
    if (error < 0) {
        error = -error;
    }
    switch (error) {
    case 0: return "no error";
    case ENOENT: return "no such entry";
    case EIO: return "input/output error";
    case ENOMEM: return "out of memory";
    case EBUSY: return "device busy";
    case ENODEV: return "no such device";
    case EINVAL: return "invalid argument";
    case ENOTSUP: return "operation not supported";
    case ENOBUFS: return "no buffer space";
    case ENETUNREACH: return "network unreachable";
    case ETIMEDOUT: return "timed out";
    case ECANCELED: return "cancelled";
    case EPIPE: return "broken pipe";
    case ENOTTY: return "inappropriate ioctl";
    case EADDRNOTAVAIL: return "address not available";
    case EINPROGRESS: return "in progress";
    case ENOTCONN: return "not connected";
    default: return "error";
    }
}
