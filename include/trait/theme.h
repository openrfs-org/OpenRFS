/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_THEME_H
#define TRAIT_THEME_H

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

struct trait_palette {
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
    /*
     * The window frame, as fvwm decorates one: a flat ground and the two
     * colours it bevels with.  It used to be a vertical ramp between a
     * top and a bottom, which is Openbox's frame; fvwm has no gradient
     * anywhere in it, and a 7-pixel border wants a relief rather than a
     * fade.  The two relief colours are not chosen - they come out of
     * src/trait_relief.h, which is fvwm's own GetHilite() and
     * GetShadow() run over the ground.
     */
    uint32_t frame_active;
    uint32_t frame_active_hi;
    uint32_t frame_active_lo;
    uint32_t frame_idle;
    uint32_t frame_idle_hi;
    uint32_t frame_idle_lo;
    uint32_t frame_ink;
    uint32_t frame_ink_dim;
};

/* The one a fresh session comes up in.  It was 4 while there were
 * five themes; the Trait one went and OpenRFS moved up. */
#define TRAIT_THEME_DEFAULT 3U

const struct trait_palette *trait_theme(void);

/* The themes that are INSTALLED.  lxappearance lists what is on the
 * machine; so does this, and a list of themes that are not here would be
 * a list of choices that do nothing. */
uint32_t trait_theme_count(void);
const char *trait_theme_name(uint32_t at);
bool trait_theme_select(uint32_t at);
uint32_t trait_theme_selected(void);
/* By name, for a caller holding the string a menu row carries. */
bool trait_theme_select_named(const char *name);

#define TRAIT_BG (trait_theme()->bg)
#define TRAIT_BG_PRELIGHT (trait_theme()->bg_prelight)
#define TRAIT_BG_ACTIVE (trait_theme()->bg_active)
#define TRAIT_BASE (trait_theme()->base)
#define TRAIT_BASE_PRELIGHT (trait_theme()->base_prelight)
#define TRAIT_FG (trait_theme()->fg)
#define TRAIT_TEXT (trait_theme()->text)
#define TRAIT_SEL_BG (trait_theme()->sel_bg)
#define TRAIT_SEL_FG (trait_theme()->sel_fg)
#define TRAIT_LINE (trait_theme()->line)
#define TRAIT_LINE_LIGHT (trait_theme()->line_light)
#define TRAIT_FRAME_ACTIVE (trait_theme()->frame_active)
#define TRAIT_FRAME_ACTIVE_HI (trait_theme()->frame_active_hi)
#define TRAIT_FRAME_ACTIVE_LO (trait_theme()->frame_active_lo)
#define TRAIT_FRAME_IDLE (trait_theme()->frame_idle)
#define TRAIT_FRAME_IDLE_HI (trait_theme()->frame_idle_hi)
#define TRAIT_FRAME_IDLE_LO (trait_theme()->frame_idle_lo)
#define TRAIT_FRAME_INK (trait_theme()->frame_ink)
#define TRAIT_FRAME_INK_DIM (trait_theme()->frame_ink_dim)

#endif /* TRAIT_THEME_H */
