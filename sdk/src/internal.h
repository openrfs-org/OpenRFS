/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_RUNTIME_INTERNAL_H
#define TRAIT_RUNTIME_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <trait/runtime.h>

struct trait_runtime_path {
    uint16_t volume;
    const char *text;
    size_t length;
};

int trait_runtime_path(const char *input, struct trait_runtime_path *result);
void trait_runtime_lock(volatile uint32_t *lock);
void trait_runtime_unlock(volatile uint32_t *lock);
size_t trait_allocation_size(const void *pointer);

#endif
