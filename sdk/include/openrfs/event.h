/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_EVENT_H
#define OPENRFS_EVENT_H

#include <stddef.h>
#include <stdint.h>

#include <openrfs/abi.h>

long openrfs_wait(struct openrfs_wait_item *items, size_t count,
    uint64_t deadline_ns);
long openrfs_timer_create(void);
long openrfs_timer_set(openrfs_handle_t timer, uint64_t deadline_ns);
long openrfs_cancel(openrfs_handle_t handle);

#endif
