/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd_desktop/theme.h>

#include "rsd_relief.h"

/*
 * FOUR THEMES, AND EVERY COLOUR IN THEM IS TAKEN FROM SOMEWHERE.
 *
 * Clearlooks and Adwaita are the palettes Debian's own gtkrc files carry;
 * Adwaita-dark is the dark variant's.  Those three are the GTK themes on
 * the machine, and nothing in them is a colour somebody liked.
 *
 * There were five.  The fourth was called RSD - this project's name
 * two names ago - and its two colours were sampled from the onion mark
 * and from the wallpaper, both of which were deleted along with the rest
 * of the artwork the project has outgrown.  A theme named after a
 * retired project, drawn from files that are not in the tree, listed in
 * Settings where somebody could pick it: that is three reasons and any
 * one of them would have done.
 *
 * The fourth is NOT a GTK theme and does not pretend to be one - it is
 * this desktop's own, and it is here because the desktop had an identity
 * that the LXDE defaults do not carry.  Its colours were not invented
 * either: they were SAMPLED from the artwork of the time.  The selection
 * gold is #B08020, one of the onion mark's ambers; the ground is
 * #212121, read out of the corner of the wallpaper PNG.
 *
 * NEITHER FILE IS IN THIS REPOSITORY ANY MORE.  The onion was the mark
 * before the mark and the wallpaper has not been decoded since the root
 * became the X weave, so both were deleted.  That is still where these
 * numbers came from and it is no longer something you can check here,
 * which is worth knowing before trusting the sentence above.
 */
static const struct rsd_palette PALETTES[] = {
    /* Clearlooks: gtk2-engines, usr/share/themes/Clearlooks/gtk-2.0/gtkrc */
    {
        0xEDECEBU, 0xF2F1F0U, 0xD5D4D3U, 0xFFFFFFU, 0xE1E0DFU,
        0x000000U, 0x1A1A1AU, 0x86ABD9U, 0xFFFFFFU, 0xB6B3AFU, 0xFAFAF9U,
        RSD_RELIEF_CLEARLOOKS_ACTIVE,
        RSD_RELIEF_CLEARLOOKS_IDLE, 0xD9DDE3U, 0x9AA0A8U
    },
    /* Adwaita's GTK2 palette, from the gtkrc gnome-themes-extra-data
     * ships beside Clearlooks', with the same shade() factors applied. */
    {
        0xEDEDEDU, 0xF2F2F2U, 0xD5D5D5U, 0xFFFFFFU, 0xE1E1E1U,
        0x2E3436U, 0x2E3436U, 0x4A90D9U, 0xFFFFFFU, 0xC3C3C3U, 0xFBFBFBU,
        RSD_RELIEF_ADWAITA_ACTIVE,
        RSD_RELIEF_ADWAITA_IDLE, 0xDEDEDEU, 0x93999FU
    },
    /* Adwaita-dark. The frame goes darker with it - a dark theme with a
     * light title bar is two themes at once. */
    {
        0x3C3C3CU, 0x464646U, 0x2E2E2EU, 0x2B2B2BU, 0x353535U,
        0xEEEEECU, 0xEEEEECU, 0x215D9CU, 0xFFFFFFU, 0x1B1B1BU, 0x4A4A4AU,
        RSD_RELIEF_ADWAITA_DARK_ACTIVE,
        RSD_RELIEF_ADWAITA_DARK_IDLE, 0xD8D8D6U, 0x8A8A88U
    },
    /*
     * RSD.  Sixteen colours and nothing between them.
     *
     * The palette is the IBM text one, which is what TempleOS drew in
     * and what tools/render.c paints the console and the installer
     * with - the same table, so the three do not end up three slightly
     * different reds.  Red is the brand's #9E1B1B rather than IBM's
     * #AA0000, for the same reason.
     *
     * Every pair here is two of those sixteen. Nothing is shaded, no
     * value is computed from another, and the two frame ends are equal
     * because a gradient needs colours that are not in the palette to
     * get from one end to the other.
     */
    {
        0xAAAAAAU,   /* bg          light grey */
        0xAAAAAAU,   /* prelight    the same: there is nothing to shade */
        0x555555U,   /* active      dark grey */
        0xFFFFFFU,   /* base        white */
        0xFFFFFFU,   /* base pre    white */
        0x000000U,   /* fg          black */
        0x000000U,   /* text        black */
        0x0000AAU,   /* sel bg      blue */
        0xFFFFFFU,   /* sel fg      white */
        0x000000U,   /* line        black */
        0xFFFFFFU,   /* line light  white, for the lit side of a bevel */
        /*
         * The frame is fvwm's, not this project's.  OpenBSD's
         * system.fvwmrc says HilightColor #bebebe blue for the focused
         * window and Color #bebebe/darkred for every other one, and
         * that is what these two lines are - ground, lit side, shadow
         * side, the last two computed by fvwm's own GetHilite() and
         * GetShadow().  The brand red is not here: a title bar in
         * #9E1B1B is this desktop wearing fvwm's shape in its own
         * colours, which is the thing it was asked not to be.
         */
        RSD_RELIEF_RSD_ACTIVE,
        RSD_RELIEF_RSD_IDLE,
        0xBEBEBEU,   /* frame ink       fvwm's #bebebe, both states */
        0xBEBEBEU    /* frame ink dim   the same: fvwm dims the GROUND */
    },

    /*
     * OXYGEN, which is what this desktop wears now - and these are
     * Oxygen's OWN colours, out of color-schemes/Oxygen.colors in KDE's
     * repository, not colours chosen to look about right.
     *
     *     [Colors:Window]    BackgroundNormal    214,210,208
     *                        BackgroundAlternate 218,217,216
     *                        ForegroundNormal    34,31,30
     *     [Colors:View]      BackgroundNormal    255,255,255
     *                        BackgroundAlternate 248,247,246
     *                        ForegroundNormal    31,28,27
     *     [Colors:Selection] BackgroundNormal    67,172,232
     *                        ForegroundNormal    255,255,255
     *     [WM]               inactiveBackground  224,223,222
     *                        inactiveForeground  75,71,67
     *
     * AND LOOK AT THE WINDOW COLOUR. 214,210,208 is not a grey - it is
     * a WARM grey, a little more red than blue, and that is most of why
     * a KDE 4 desktop feels the way it does rather than looking like a
     * sheet of aluminium. The first pass here used 0xEDEDED, which is
     * both neutral and much too light: a window in it has nowhere to go
     * when it is shaded, which is why the gradient kept coming out
     * flat.
     *
     * The frame colour IS the window colour, and that is the point:
     * Oxygen has no separate title bar colour because it has no
     * separate title bar. ([WM] activeBackground in the scheme is a
     * bright blue, and it is what a decoration that draws its own bar -
     * Plastik, Laptop - would use. Oxygen does not.)
     *
     * Nothing here is shaded at rest. Every lit surface gets its shade
     * at draw time from rsd_oxy_window(), sampled at the pixel's
     * absolute position in its window, and the shades themselves come
     * from tools/make-glass.py running KColorScheme's own HCY
     * arithmetic over these bases.
     */
    {
        0xD6D2D0U,   /* bg            Window BackgroundNormal */
        0xDAD9D8U,   /* prelight      Window BackgroundAlternate */
        0xB3ABA7U,   /* active        the window's own dark shade */
        0xFFFFFFU,   /* base          View BackgroundNormal */
        0xF8F7F6U,   /* base prelight View BackgroundAlternate */
        0x221F1EU,   /* fg            Window ForegroundNormal */
        0x1F1C1BU,   /* text          View ForegroundNormal */
        0x43ACE8U,   /* sel bg        Selection BackgroundNormal */
        0xFFFFFFU,   /* sel fg        Selection ForegroundNormal */
        0xB3ABA7U,   /* line          the dark shade again */
        0xFCFCFCU,   /* line light    the light one */

        0xD6D2D0U,   /* frame active     the window colour itself */
        0xFCFCFCU,   /* frame active hi  its light shade */
        0xB3ABA7U,   /* frame active lo  its dark shade */
        0xE0DFDEU,   /* frame idle       [WM] inactiveBackground */
        0xFFFFFFU,   /* frame idle hi */
        0xB8B3AEU,   /* frame idle lo */
        0x221F1EU,   /* frame ink        Window ForegroundNormal */
        0x4B4743U    /* frame ink dim    [WM] inactiveForeground */
    }
};

static const char *const NAMES[] = {
    "Clearlooks", "Adwaita", "Adwaita-dark", "RSD", "Oxygen"
};

#define THEME_COUNT (sizeof(PALETTES) / sizeof(PALETTES[0]))

/* RSD.  The LXDE themes are still in the list, one press of the
 * Widget row away, because they are installed and a list that hides
 * what is installed is a shorter list than the machine has. */
static uint32_t current = RSD_THEME_DEFAULT;

const struct rsd_palette *rsd_theme(void)
{
    return &PALETTES[current];
}

uint32_t rsd_theme_count(void)
{
    return (uint32_t)THEME_COUNT;
}

const char *rsd_theme_name(uint32_t at)
{
    if (at >= THEME_COUNT) {
        return "";
    }
    return NAMES[at];
}

bool rsd_theme_select(uint32_t at)
{
    if (at >= THEME_COUNT) {
        return false;
    }
    current = at;
    return true;
}

uint32_t rsd_theme_selected(void)
{
    return current;
}

bool rsd_theme_select_named(const char *name)
{
    uint32_t at;

    if (name == NULL) {
        return false;
    }
    for (at = 0U; at < THEME_COUNT; ++at) {
        uint32_t byte = 0U;

        while (NAMES[at][byte] != '\0' && name[byte] != '\0' &&
                NAMES[at][byte] == name[byte]) {
            ++byte;
        }
        if (NAMES[at][byte] == '\0' && name[byte] == '\0') {
            current = at;
            return true;
        }
    }
    return false;
}
