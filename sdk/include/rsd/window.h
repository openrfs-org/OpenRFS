/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_WINDOW_H
#define RSD_WINDOW_H

#include <stddef.h>
#include <stdint.h>
#include <rsd/abi.h>

int rsd_window_create(const char *title, uint32_t width, uint32_t height,
    struct rsd_window_create_response *response);
long rsd_surface_present(rsd_handle_t window,
    const struct rsd_rect *rectangles, size_t count);
long rsd_event_read(rsd_handle_t events, struct rsd_event *event);
long rsd_event_wait(rsd_handle_t events, uint64_t deadline_ns);
long rsd_pointer_capture(rsd_handle_t window, int capture);

#endif
