/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_MINIMAL_DE_H
#define OPENRFS_MINIMAL_DE_H

#include <stdbool.h>
#include <stdint.h>

#include <openrfs/ui.h>

bool minimal_de_construct(uint32_t *pixels, uint32_t width, uint32_t height);
void minimal_de_draw(void);
void minimal_de_draw_overlays(void);
bool minimal_de_event(const struct ui_event *event);
bool minimal_de_overlay_open(void);
bool minimal_de_terminal_client(struct ui_rect *out);
bool minimal_de_self_test(void);

#endif /* OPENRFS_MINIMAL_DE_H */
