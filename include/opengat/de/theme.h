/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_DE_THEME_H
#define OPENGAT_DE_THEME_H

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

struct opengat_palette {
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

const struct opengat_palette *opengat_theme(void);

/* The themes that are INSTALLED.  lxappearance lists what is on the
 * machine; so does this, and a list of themes that are not here would be
 * a list of choices that do nothing. */
uint32_t opengat_theme_count(void);
const char *opengat_theme_name(uint32_t at);
bool opengat_theme_select(uint32_t at);
uint32_t opengat_theme_selected(void);
/* By name, for a caller holding the string a menu row carries. */
bool opengat_theme_select_named(const char *name);

#define OPENGAT_BG (opengat_theme()->bg)
#define OPENGAT_BG_PRELIGHT (opengat_theme()->bg_prelight)
#define OPENGAT_BG_ACTIVE (opengat_theme()->bg_active)
#define OPENGAT_BASE (opengat_theme()->base)
#define OPENGAT_BASE_PRELIGHT (opengat_theme()->base_prelight)
#define OPENGAT_FG (opengat_theme()->fg)
#define OPENGAT_TEXT (opengat_theme()->text)
#define OPENGAT_SEL_BG (opengat_theme()->sel_bg)
#define OPENGAT_SEL_FG (opengat_theme()->sel_fg)
#define OPENGAT_LINE (opengat_theme()->line)
#define OPENGAT_LINE_LIGHT (opengat_theme()->line_light)
#define OPENGAT_FRAME_ACTIVE_TOP (opengat_theme()->frame_active_top)
#define OPENGAT_FRAME_ACTIVE_BOTTOM (opengat_theme()->frame_active_bottom)
#define OPENGAT_FRAME_IDLE_TOP (opengat_theme()->frame_idle_top)
#define OPENGAT_FRAME_IDLE_BOTTOM (opengat_theme()->frame_idle_bottom)
#define OPENGAT_FRAME_INK (opengat_theme()->frame_ink)
#define OPENGAT_FRAME_INK_DIM (opengat_theme()->frame_ink_dim)

#endif /* OPENGAT_DE_THEME_H */
