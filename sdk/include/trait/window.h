/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_WINDOW_H
#define TRAIT_WINDOW_H

#include <stddef.h>
#include <stdint.h>
#include <trait/abi.h>

int trait_window_create(const char *title, uint32_t width, uint32_t height,
    struct trait_window_create_response *response);
long trait_surface_present(trait_handle_t window,
    const struct trait_rect *rectangles, size_t count);
long trait_event_read(trait_handle_t events, struct trait_event *event);
long trait_event_wait(trait_handle_t events, uint64_t deadline_ns);
long trait_pointer_capture(trait_handle_t window, int capture);

#endif
