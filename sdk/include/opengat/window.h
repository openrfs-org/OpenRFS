/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_WINDOW_H
#define OPENGAT_WINDOW_H

#include <stddef.h>
#include <stdint.h>
#include <opengat/abi.h>

int opengat_window_create(const char *title, uint32_t width, uint32_t height,
    struct opengat_window_create_response *response);
long opengat_surface_present(opengat_handle_t window,
    const struct opengat_rect *rectangles, size_t count);
long opengat_event_read(opengat_handle_t events, struct opengat_event *event);
long opengat_event_wait(opengat_handle_t events, uint64_t deadline_ns);
long opengat_pointer_capture(opengat_handle_t window, int capture);

#endif
