/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_RUNTIME_INTERNAL_H
#define OPENRFS_RUNTIME_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <openrfs/runtime.h>

struct openrfs_runtime_path {
    uint16_t volume;
    const char *text;
    size_t length;
};

int openrfs_runtime_path(const char *input, struct openrfs_runtime_path *result);
void openrfs_runtime_lock(volatile uint32_t *lock);
void openrfs_runtime_unlock(volatile uint32_t *lock);
size_t openrfs_allocation_size(const void *pointer);

#endif
