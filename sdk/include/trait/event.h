/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_EVENT_H
#define TRAIT_EVENT_H

#include <stddef.h>
#include <stdint.h>

#include <trait/abi.h>

long trait_wait(struct trait_wait_item *items, size_t count,
    uint64_t deadline_ns);
long trait_timer_create(void);
long trait_timer_set(trait_handle_t timer, uint64_t deadline_ns);
long trait_cancel(trait_handle_t handle);

#endif
