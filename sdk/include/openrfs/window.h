/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_WINDOW_H
#define OPENRFS_WINDOW_H

#include <stddef.h>
#include <stdint.h>
#include <openrfs/abi.h>

int openrfs_window_create(const char *title, uint32_t width, uint32_t height,
    struct openrfs_window_create_response *response);
long openrfs_surface_present(openrfs_handle_t window,
    const struct openrfs_rect *rectangles, size_t count);
long openrfs_event_read(openrfs_handle_t events, struct openrfs_event *event);
long openrfs_event_wait(openrfs_handle_t events, uint64_t deadline_ns);
long openrfs_pointer_capture(openrfs_handle_t window, int capture);

#endif
