/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_KERNEL_DESKTOP_H
#define RSD_KERNEL_DESKTOP_H

#include <stdbool.h>
#include <stdint.h>

#include <rsd/ui.h>

bool rsd_desktop_construct(uint32_t *pixels, uint32_t width, uint32_t height);
void rsd_desktop_draw(void);
void rsd_desktop_draw_overlays(void);
bool rsd_desktop_event(const struct ui_event *event);
bool rsd_desktop_overlay_open(void);
bool rsd_desktop_terminal_client(struct ui_rect *out);
bool rsd_desktop_self_test(void);

#endif /* RSD_KERNEL_DESKTOP_H */
