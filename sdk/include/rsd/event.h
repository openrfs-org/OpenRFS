/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_EVENT_H
#define RSD_EVENT_H

#include <stddef.h>
#include <stdint.h>

#include <rsd/abi.h>

long rsd_wait(struct rsd_wait_item *items, size_t count,
    uint64_t deadline_ns);
long rsd_timer_create(void);
long rsd_timer_set(rsd_handle_t timer, uint64_t deadline_ns);
long rsd_cancel(rsd_handle_t handle);

#endif
