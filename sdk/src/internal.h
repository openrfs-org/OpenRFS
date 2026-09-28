/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_RUNTIME_INTERNAL_H
#define RSD_RUNTIME_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <rsd/runtime.h>

struct rsd_runtime_path {
    uint16_t volume;
    const char *text;
    size_t length;
};

int rsd_runtime_path(const char *input, struct rsd_runtime_path *result);
void rsd_runtime_lock(volatile uint32_t *lock);
void rsd_runtime_unlock(volatile uint32_t *lock);
size_t rsd_allocation_size(const void *pointer);

#endif
