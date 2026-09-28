#!/usr/bin/env python3
"""The glass, as colours rather than as guesses.

    python3 tools/make-glass.py

WHAT WAS WRONG BEFORE.

The first cut of this desktop's Oxygen shaded colours by scaling every
channel - light was base * 118/100, dark * 82/100 - which was honest
about being a stand-in and still wrong in a way you could see. Scaling a
light grey by 1.12 clips: 0xED * 112 / 100 is 265, which is 255, so the
top of every window came out pure white and the gradient that was
supposed to run down it had nothing left to run from. A window with a
white bar at the top and a flat grey body is not a lit sheet.

KDE does not scale channels. It works in HCY - a luma-based space - and
moves the LUMA by an amount that depends on how light the colour already
is. That is why an Oxygen window never blows out at the top.

WHAT IS IMPLEMENTED HERE, quoted from the sources:

  kcolorscheme.cpp, KColorScheme::shade(color, role, contrast):

      qreal y = KColorUtils::luma(color), yi = 1.0 - y;
      qreal lightAmount = (0.05 + y * 0.55) * (0.25 + contrast * 0.75);
      qreal darkAmount  = (-y) * (0.55 + contrast * 0.35);
      LightShade:    shade(color, lightAmount)
      MidlightShade: shade(color, (0.15 + 0.35 * yi) * lightAmount)
      MidShade:      shade(color, (0.35 + 0.15 * y) * darkAmount)
      DarkShade:     shade(color, darkAmount)
      ShadowShade:   darken(shade(color, darkAmount), 0.5 + 0.3 * y)

      and the default contrast is 0.7 - contrastF() reads
      KDE/contrast, whose default is 7, and returns a tenth of it.

  kcolorspaces.cpp, KHCY: luma is 0.2126 r + 0.7152 g + 0.0722 b on
      GAMMA-EXPANDED channels, gamma being pow(n, 2.2).

  kcolorutils.cpp:
      shade(color, ky):  c.y = normalize(c.y + ky)
      darken(color, ky): c.y = normalize(c.y * (1.0 - ky))

  oxygenhelper.cpp:
      calcLightColor  = shade(color, LightShade, contrast)
      calcDarkColor   = shade(color, MidShade, contrast)
      calcShadowColor = shade(mix(black, color, alpha), ShadowShade, ...)

      _bgcontrast = qMin(1.0, 0.9 * _contrast / 0.7)        -> 0.9

      backgroundTopColor:
          my = luma(shade(color, LightShade, 0.0))
          by = luma(color)
          shade(color, (my - by) * _bgcontrast)
      backgroundBottomColor: the same with MidShade
      backgroundRadialColor: shade(color, LightShade, _bgcontrast)

HCY is implemented in full, from the same file's KHCY constructor and
qColor(), because Oxygen's own window colour - 214,210,208 - is not a
grey. It is a WARM grey, and an approximation that scaled the three
channels together would quietly take the warmth out of every shade
derived from it.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "desktop" / "src" / "rsd_glass.h"

CONTRAST = 0.7
BG_CONTRAST = min(1.0, 0.9 * CONTRAST / 0.7)

YC = (0.2126, 0.7152, 0.0722)


def normalize(n):
    return 0.0 if n < 0.0 else (1.0 if n > 1.0 else n)


def wrap(a, d=1.0):
    r = a % d
    return d + r if r < 0.0 else (r if r > 0.0 else 0.0)


def gamma(n):
    return normalize(n) ** 2.2


def igamma(n):
    return normalize(n) ** (1.0 / 2.2)


def channels(colour):
    return ((colour >> 16) & 0xFF, (colour >> 8) & 0xFF, colour & 0xFF)


def pack(rgb):
    out = 0
    for v in rgb:
        out = (out << 8) | min(255, max(0, int(round(v))))
    return out


YC = (0.2126, 0.7152, 0.0722)


def lumag(r, g, b):
    return r * YC[0] + g * YC[1] + b * YC[2]


def to_hcy(colour):
    """KHCY's constructor, from kcolorspaces.cpp."""
    r, g, b = (gamma(v / 255.0) for v in channels(colour))
    y = lumag(r, g, b)

    p = max(r, g, b)
    n = min(r, g, b)
    d = 6.0 * (p - n)
    if n == p:
        h = 0.0
    elif r == p:
        h = (g - b) / d
    elif g == p:
        h = ((b - r) / d) + (1.0 / 3.0)
    else:
        h = ((r - g) / d) + (2.0 / 3.0)

    if r == g == b:
        c = 0.0
    else:
        c = max((y - n) / y, (p - y) / (1 - y))
    return h, c, y


def from_hcy(h, c, y):
    """KHCY::qColor(), from the same file."""
    h = wrap(h)
    c = normalize(c)
    y = normalize(y)
    hs = h * 6.0

    if hs < 1.0:
        th, tm = hs, YC[0] + YC[1] * hs
    elif hs < 2.0:
        th = 2.0 - hs
        tm = YC[1] + YC[0] * th
    elif hs < 3.0:
        th = hs - 2.0
        tm = YC[1] + YC[2] * th
    elif hs < 4.0:
        th = 4.0 - hs
        tm = YC[2] + YC[1] * th
    elif hs < 5.0:
        th = hs - 4.0
        tm = YC[2] + YC[0] * th
    else:
        th = 6.0 - hs
        tm = YC[0] + YC[2] * th

    if tm >= y:
        tp = y + y * c * (1.0 - tm) / tm if tm > 0.0 else y
        to = y + y * c * (th - tm) / tm if tm > 0.0 else y
        tn = y - (y * c)
    else:
        tp = y + (1.0 - y) * c
        to = y + (1.0 - y) * c * (th - tm) / (1.0 - tm)
        tn = y - (1.0 - y) * c * tm / (1.0 - tm)

    tp, to, tn = (igamma(v) * 255.0 for v in (tp, to, tn))
    if hs < 1.0:
        return pack((tp, to, tn))
    if hs < 2.0:
        return pack((to, tp, tn))
    if hs < 3.0:
        return pack((tn, tp, to))
    if hs < 4.0:
        return pack((tn, to, tp))
    if hs < 5.0:
        return pack((to, tn, tp))
    return pack((tp, tn, to))


def luma(colour):
    r, g, b = (gamma(v / 255.0) for v in channels(colour))
    return lumag(r, g, b)


def shade_by(colour, amount):
    """KColorUtils::shade: c.y = normalize(c.y + ky), hue and chroma
    left alone."""
    h, c, y = to_hcy(colour)
    return from_hcy(h, c, normalize(y + amount))


def darken_by(colour, amount):
    """KColorUtils::darken: c.y = normalize(c.y * (1.0 - ky))."""
    h, c, y = to_hcy(colour)
    return from_hcy(h, c, normalize(y * (1.0 - amount)))


def scheme_shade(colour, role, contrast=CONTRAST):
    y = luma(colour)
    yi = 1.0 - y
    light = (0.05 + y * 0.55) * (0.25 + contrast * 0.75)
    dark = (-y) * (0.55 + contrast * 0.35)

    if y < 0.006 or y > 0.93:
        # Neither branch is reached by any base this desktop uses; they
        # are here so the tool does not silently do the wrong thing if
        # one ever is.
        raise SystemExit("colour 0x%06X is outside the range this tool "
                         "implements (luma %.3f)" % (colour, y))

    if role == "light":
        return shade_by(colour, light)
    if role == "midlight":
        return shade_by(colour, (0.15 + 0.35 * yi) * light)
    if role == "mid":
        return shade_by(colour, (0.35 + 0.15 * y) * dark)
    if role == "dark":
        return shade_by(colour, dark)
    return darken_by(shade_by(colour, dark), 0.5 + 0.3 * y)


def derive(base):
    top_amount = (luma(scheme_shade(base, "light", 0.0)) - luma(base)) \
        * BG_CONTRAST
    bottom_amount = (luma(scheme_shade(base, "mid", 0.0)) - luma(base)) \
        * BG_CONTRAST
    return {
        "top": shade_by(base, top_amount),
        "bottom": shade_by(base, bottom_amount),
        "radial": scheme_shade(base, "light", BG_CONTRAST),
        "light": scheme_shade(base, "light"),
        "dark": scheme_shade(base, "mid"),
        "shadow": scheme_shade(base, "shadow"),
    }


# The bases this desktop lights: the window colour, and the inactive
# window's. src/theme.c holds them; they are repeated here because this
# tool has to be runnable on its own, and the generated header is
# checked against theme.c by the shell's own self test.
BASES = [
    ("RSD_GLASS_ACTIVE", 0xD6D2D0, "the window, and the focused frame"),
    ("RSD_GLASS_IDLE", 0xE0DFDE, "an unfocused window's frame"),
]

ORDER = ["top", "bottom", "radial", "light", "dark", "shadow"]

# Helper::radialGradient()'s own stops.
POOL_STOPS = [(0.0, 255), (0.5, 101), (0.75, 37), (1.0, 0)]


def pool_alpha(squared):
    """Alpha at a squared normalised radius of `squared` / 255."""
    t = (squared / 255.0) ** 0.5
    if t >= 1.0:
        return 0
    for at in range(len(POOL_STOPS) - 1):
        t0, a0 = POOL_STOPS[at]
        t1, a1 = POOL_STOPS[at + 1]
        if t <= t1:
            return int(round(a0 + (a1 - a0) * (t - t0) / (t1 - t0)))
    return 0


def main():
    with open(OUT, "w") as f:
        f.write("/* SPDX-License-Identifier: GPL-3.0-only */\n")
        f.write("/*\n"
                " * GENERATED by tools/make-glass.py - do not edit.\n"
                " *\n"
                " * Oxygen's derived colours for this desktop's bases,\n"
                " * put through KColorScheme::shade() in HCY rather than\n"
                " * scaled per channel. Scaling clips: a light grey times\n"
                " * 1.12 is white, and a window whose top is white has no\n"
                " * gradient left to run down it.\n"
                " *\n"
                " * tools/make-glass.py quotes every formula and names\n"
                " * the file it came out of.\n"
                " */\n")
        f.write("#ifndef RSD_GLASS_H\n#define RSD_GLASS_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("/*\n"
                " * THE SPLIT, from oxygenhelper.cpp\n"
                " * renderWindowBackground():\n"
                " *\n"
                " *     const int splitY(qMin(300.0,"
                " (3 * windowRect.height()) / 4));\n"
                " *\n"
                " * Above it the window runs top -> base -> bottom;\n"
                " * below it it is flat. A tall window settles instead of\n"
                " * getting darker forever.\n"
                " */\n")
        f.write("#define RSD_GLASS_SPLIT 300U\n\n")
        f.write("/*\n"
                " * THE POOL OF LIGHT AT THE TOP, which is the other half\n"
                " * of renderWindowBackground() and the half that gets\n"
                " * left out: a radial highlight 64 pixels tall, at most\n"
                " * 600 wide, centred on the top edge. Its stops are\n"
                " * alpha 255, 101, 37, 0 at 0, 0.5, 0.75 and 1 of the\n"
                " * radius, out of Helper::radialGradient().\n"
                " */\n")
        f.write("#define RSD_GLASS_POOL_WIDE 600U\n")
        f.write("#define RSD_GLASS_POOL_HIGH 64U\n\n")
        f.write("/*\n"
                " * The pool's alpha, indexed by the SQUARED distance\n"
                " * from its centre - 0 at the middle of the top edge, 255\n"
                " * where the light has run out:\n"
                " *\n"
                " *     q = dx * dx * 256 / (WIDE / 2)^2\n"
                " *       + dy * dy * 256 / HIGH^2\n"
                " *\n"
                " * Squared, so the shell needs no square root; the stops\n"
                " * are interpolated here against the root of it, which is\n"
                " * what Qt does along the radius.\n"
                " */\n")
        f.write("static const uint8_t rsd_glass_pool[256] = {\n")
        for at in range(0, 256, 16):
            f.write("    " + "".join("%4d," % pool_alpha(q)
                                     for q in range(at, at + 16)) + "\n")
        f.write("};\n\n")

        for name, base, what in BASES:
            colours = derive(base)
            f.write("/* %s: 0x%06X */\n" % (what, base))
            f.write("#define %s 0x%06XU\n" % (name, base))
            for role in ORDER:
                f.write("#define %s_%s 0x%06XU\n"
                        % (name, role.upper(), colours[role]))
            f.write("\n")
        f.write("#endif /* RSD_GLASS_H */\n")

    print("wrote %s" % OUT.relative_to(ROOT))
    for name, base, _ in BASES:
        colours = derive(base)
        print("  0x%06X  luma %.3f" % (base, luma(base)))
        for role in ORDER:
            print("      %-7s 0x%06X" % (role, colours[role]))


main()
