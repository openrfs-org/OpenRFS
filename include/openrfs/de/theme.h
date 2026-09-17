/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DE_THEME_H
#define OPENRFS_DE_THEME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * THE PALETTE, AND IT IS RUNTIME STATE RATHER THAN CONSTANTS.
 *
 * It began as #defines, which was right while there was one theme and
 * wrong the moment Settings offered a list of them: a Widget page that
 * lists themes and cannot apply one is a control that does not do what it
 * is drawn as, which is the rule this desktop is built on.
 *
 * The names below are still used exactly as the constants were - every
 * call site is unchanged - because each expands to a read of the current
 * palette.  That is the point: making the theme switchable should not
 * mean touching every line that draws.
 *
 * Clearlooks' values are its own, out of its gtkrc in Debian's
 * gtk2-engines (usr/share/themes/Clearlooks/gtk-2.0/gtkrc).  GTK computes
 * three of them at runtime with shade(); a framebuffer has no runtime to
 * compute them in, so they are precomputed with the factor named beside
 * each.
 */

struct openrfs_palette {
    uint32_t bg;
    uint32_t bg_prelight;    /* shade(1.02, bg) */
    uint32_t bg_active;      /* shade(0.90, bg) */
    uint32_t base;
    uint32_t base_prelight;  /* shade(0.95, bg) */
    uint32_t fg;
    uint32_t text;
    uint32_t sel_bg;
    uint32_t sel_fg;
    uint32_t line;
    uint32_t line_light;
    /* The window frame: Openbox draws the title bar as a vertical ramp,
     * so these are its two ends. */
    uint32_t frame_active_top;
    uint32_t frame_active_bottom;
    uint32_t frame_idle_top;
    uint32_t frame_idle_bottom;
    uint32_t frame_ink;
    uint32_t frame_ink_dim;
};

const struct openrfs_palette *openrfs_theme(void);

/* The themes that are INSTALLED.  lxappearance lists what is on the
 * machine; so does this, and a list of themes that are not here would be
 * a list of choices that do nothing. */
uint32_t openrfs_theme_count(void);
const char *openrfs_theme_name(uint32_t at);
bool openrfs_theme_select(uint32_t at);
uint32_t openrfs_theme_selected(void);
/* By name, for a caller holding the string a menu row carries. */
bool openrfs_theme_select_named(const char *name);

#define OPENRFS_BG (openrfs_theme()->bg)
#define OPENRFS_BG_PRELIGHT (openrfs_theme()->bg_prelight)
#define OPENRFS_BG_ACTIVE (openrfs_theme()->bg_active)
#define OPENRFS_BASE (openrfs_theme()->base)
#define OPENRFS_BASE_PRELIGHT (openrfs_theme()->base_prelight)
#define OPENRFS_FG (openrfs_theme()->fg)
#define OPENRFS_TEXT (openrfs_theme()->text)
#define OPENRFS_SEL_BG (openrfs_theme()->sel_bg)
#define OPENRFS_SEL_FG (openrfs_theme()->sel_fg)
#define OPENRFS_LINE (openrfs_theme()->line)
#define OPENRFS_LINE_LIGHT (openrfs_theme()->line_light)
#define OPENRFS_FRAME_ACTIVE_TOP (openrfs_theme()->frame_active_top)
#define OPENRFS_FRAME_ACTIVE_BOTTOM (openrfs_theme()->frame_active_bottom)
#define OPENRFS_FRAME_IDLE_TOP (openrfs_theme()->frame_idle_top)
#define OPENRFS_FRAME_IDLE_BOTTOM (openrfs_theme()->frame_idle_bottom)
#define OPENRFS_FRAME_INK (openrfs_theme()->frame_ink)
#define OPENRFS_FRAME_INK_DIM (openrfs_theme()->frame_ink_dim)

#endif /* OPENRFS_DE_THEME_H */
