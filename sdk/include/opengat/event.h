/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_EVENT_H
#define OPENGAT_EVENT_H

#include <stddef.h>
#include <stdint.h>

#include <opengat/abi.h>

long opengat_wait(struct opengat_wait_item *items, size_t count,
    uint64_t deadline_ns);
long opengat_timer_create(void);
long opengat_timer_set(opengat_handle_t timer, uint64_t deadline_ns);
long opengat_cancel(opengat_handle_t handle);

#endif
