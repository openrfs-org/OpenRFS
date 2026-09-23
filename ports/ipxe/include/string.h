/* SPDX-License-Identifier: GPL-3.0-only */
/* The string functions the vendored drivers call; see ports/ipxe/libc.c. */
#ifndef OPENRFS_IPXE_STRING_H
#define OPENRFS_IPXE_STRING_H

#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t length);
void *memmove(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);
int memcmp(const void *left, const void *right, size_t length);
void *memchr(const void *source, int character, size_t length);
size_t strlen(const char *text);
size_t strnlen(const char *text, size_t maximum);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t length);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t length);
char *strchr(const char *text, int character);
char *strrchr(const char *text, int character);
char *strcat(char *destination, const char *source);
const char *strerror(int error);

#endif
