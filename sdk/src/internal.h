/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_RUNTIME_INTERNAL_H
#define OPENGAT_RUNTIME_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <opengat/runtime.h>

struct opengat_runtime_path {
    uint16_t volume;
    const char *text;
    size_t length;
};

int opengat_runtime_path(const char *input, struct opengat_runtime_path *result);
void opengat_runtime_lock(volatile uint32_t *lock);
void opengat_runtime_unlock(volatile uint32_t *lock);
size_t opengat_allocation_size(const void *pointer);

#endif
