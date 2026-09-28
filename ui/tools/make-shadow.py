#!/usr/bin/env python3
"""The window shadow, which is the thing that says 2010.

    python3 tools/make-shadow.py

WHY THIS EXISTS AT ALL.

A KDE 4 window is not a rectangle on a wallpaper: it sits ABOVE the
wallpaper and casts onto it, and the focused window casts a blue-cyan
GLOW rather than a shadow.  That halo is the most recognisable single
feature of the look - more than the gradient, more than the rounding.
Without it the windows are stickers; with it they float.

THE NUMBERS ARE OXYGEN'S OWN, out of two files:

  liboxygen/oxygenactiveshadowconfigdata.kcfg
      ShadowSize     40
      VerticalOffset 0
      InnerColor     112,239,255
      OuterColor     84,167,240
      UseOuterColor  true

  liboxygen/oxygeninactiveshadowconfigdata.kcfg
      ShadowSize     40
      VerticalOffset 0.2
      InnerColor     0,0,0
      OuterColor     0,0,0
      UseOuterColor  false

and the gradients they are put through, out of
liboxygen/oxygenshadowcache.cpp ShadowCache::pixmap():

      static const qreal fixedSize = 25.5;
      const int overlap = 4;

  active, inner (sharp):   radius min(S, (S + fixedSize) / 2)
                           Gaussian(0.85, 0.17), InnerColor
  active, outer (spread):  radius S
                           Gaussian(0.46, 0.34), OuterColor
  idle, inner (sharp):     radius min(S, fixedSize)
                           Parabolic(1.0, 0.22)
  idle, mid:               radius min(S, (S + 2 * fixedSize) / 3)
                           Gaussian(0.54, 0.21)
  idle, outer (spread):    radius S
                           Gaussian(0.155, 0.445)

with, from the same file,

      Gaussian:  amplitude * exp(-(x / width)^2 - 0.05)
      Parabolic: amplitude * (1 - (x / width)^2)

where x is the radius normalised to that gradient's own size, and each
layer is painted OVER the one before it.

WHY A TABLE.  The shell is freestanding C with no floating point and no
exp(), so the curves are evaluated here and the result compiled in.  The
table is indexed by the SQUARED distance from the window's edge, which is
what lets the drawing code place a radial falloff round a rectangle
without a square root: at a corner the distance is sqrt(dx^2 + dy^2), and
dx^2 + dy^2 is exactly what it has.

ONE APPROXIMATION, named so it is not mistaken for the real thing.
Oxygen gives each layer of the inactive shadow its own vertical offset -
0.2, ~2 and 4 pixels - so the three are not concentric.  Here one offset
is applied to the whole shadow, the middle layer's, because the table is
one-dimensional.  The active shadow's offset is 0 in Oxygen's own
configuration, so for the focused window - the one anybody looks at -
there is nothing to approximate.
"""
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "desktop" / "src" / "rsd_shadow.h"

FIXED = 25.5
OVERLAP = 4
SIZE = 40 + OVERLAP          # shadowSize += overlap
# What is actually visible: the window covers the first
# `overlap` pixels of its own shadow.
REACH = SIZE - OVERLAP


def gaussian(amplitude, width):
    return lambda x: max(0.0, amplitude * math.exp(-(x / width) ** 2 - 0.05))


def parabolic(amplitude, width):
    return lambda x: max(0.0, amplitude * (1.0 - (x / width) ** 2))


# (radius, curve, colour) per layer, painted in this order.
ACTIVE = [
    (min(SIZE, (SIZE + FIXED) / 2), gaussian(0.85, 0.17), (112, 239, 255)),
    (SIZE, gaussian(0.46, 0.34), (84, 167, 240)),
]

IDLE = [
    (min(SIZE, FIXED), parabolic(1.0, 0.22), (0, 0, 0)),
    (min(SIZE, (SIZE + 2 * FIXED) / 3), gaussian(0.54, 0.21), (0, 0, 0)),
    (SIZE, gaussian(0.155, 0.445), (0, 0, 0)),
]

# The one offset, the middle layer's:
#     min(8 * (gradientSize * 0.2) / fixedSize, 4)
IDLE_VOFFSET = int(round(min(8.0 * (min(SIZE, (SIZE + 2 * FIXED) / 3)
                                    * 0.2) / FIXED, 4.0)))


def composite(layers, distance):
    """Straight (colour, alpha) at `distance` px from the window's edge."""
    out = (0.0, 0.0, 0.0)
    alpha = 0.0
    for radius, curve, colour in layers:
        if distance >= radius:
            continue
        a = curve(distance / radius)
        if a <= 0.0:
            continue
        # Source-over, straight colours: this layer goes on top of
        # everything painted before it.
        out = tuple(colour[i] * a + out[i] * alpha * (1.0 - a)
                    for i in range(3))
        alpha = a + alpha * (1.0 - a)
        if alpha > 0.0:
            out = tuple(c / alpha for c in out)
    return out, alpha


def table(layers):
    """One entry per squared distance, so the shell needs no root.

    Indexed from the window's OUTER EDGE, and evaluated OVERLAP pixels
    further along the curve than that: Oxygen's decoration sits four
    pixels over its own shadow tile - that is what `const int overlap = 4`
    is doing in oxygendecoration.cpp - so the innermost four pixels of
    every one of these gradients are under the window and never seen. Read
    from zero instead and the inactive shadow starts at the parabolic's
    full amplitude, which is a hard black line round the window.
    """
    rows = []
    for squared in range(REACH * REACH + 1):
        (r, g, b), a = composite(layers, math.sqrt(squared) + OVERLAP)
        rows.append((min(255, int(round(a * 255.0))),
                     min(255, int(round(r))),
                     min(255, int(round(g))),
                     min(255, int(round(b)))))
    return rows


def main():
    active = table(ACTIVE)
    idle = table(IDLE)

    with open(OUT, "w") as f:
        f.write("/* SPDX-License-Identifier: GPL-3.0-only */\n")
        f.write("/*\n"
                " * GENERATED by tools/make-shadow.py - do not edit.\n"
                " *\n"
                " * Oxygen's window shadow, as two tables of 0xAARRGGBB\n"
                " * indexed by the SQUARED distance in pixels from the\n"
                " * window's edge - so a radial falloff can be drawn round\n"
                " * a rectangle with no square root and no floating point.\n"
                " *\n"
                " * The focused window's is a blue-cyan GLOW, not a grey\n"
                " * shadow: Oxygen's own ActiveShadow is InnerColor\n"
                " * 112,239,255 over OuterColor 84,167,240. That halo is\n"
                " * the most recognisable thing about the look.\n"
                " *\n"
                " * tools/make-shadow.py quotes the configuration files\n"
                " * and the gradients these came out of.\n"
                " */\n")
        f.write("#ifndef RSD_SHADOW_H\n#define RSD_SHADOW_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("/* How far the shadow reaches from the window's edge:\n"
                " * Oxygen's ShadowSize of 40, whose innermost 4 pixels\n"
                " * are under the decoration and never seen. */\n")
        f.write("#define RSD_SHADOW_SIZE %dU\n" % REACH)
        f.write("#define RSD_SHADOW_SPAN %dU\n" % (REACH * REACH + 1))
        f.write("/* The inactive shadow hangs slightly below the window;\n"
                " * the active one does not. Oxygen's own offsets. */\n")
        f.write("#define RSD_SHADOW_VOFFSET %dU\n\n" % IDLE_VOFFSET)

        for name, rows in (("rsd_shadow_active", active),
                           ("rsd_shadow_idle", idle)):
            f.write("static const uint32_t %s[RSD_SHADOW_SPAN] = {\n"
                    % name)
            for at in range(0, len(rows), 8):
                f.write("    " + "".join("0x%02X%02X%02X%02XU,"
                                         % cell
                                         for cell in rows[at:at + 8]) + "\n")
            f.write("};\n\n")
        f.write("#endif /* RSD_SHADOW_H */\n")

    print("wrote %s: reach %d px, %d entries each, voffset %d, %d KiB"
          % (OUT.relative_to(ROOT), REACH, len(active), IDLE_VOFFSET,
             OUT.stat().st_size // 1024))
    for label, rows in (("active", active), ("idle", idle)):
        print("  %s alpha at the edge %d, at 8 px %d, at 20 px %d, "
              "at 40 px %d" % (label, rows[0][0], rows[64][0],
                               rows[400][0], rows[1600][0]))
    print("  active colour at the edge %s, at 20 px %s"
          % (active[0][1:], active[400][1:]))


main()
