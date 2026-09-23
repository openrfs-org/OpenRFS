#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Project glxgears' three gears into the outlines the shell fills.

    python3 tools/make-gears.py [out.h]

The root window is glxgears.  Not a picture of it - the gears are the
ones Brian Paul's gears.c builds, with his radii, his tooth counts, his
placement and his view angles, turned here into flat outlines that the C
side scales to whatever screen it finds.

From gears.c (Mesa demos, src/xdemos/glxgears.c):

    gear(inner, outer, width, teeth, tooth_depth)
      r0 = inner
      r1 = outer - tooth_depth / 2
      r2 = outer + tooth_depth / 2
      da = 2 pi / teeth / 4
    and a tooth runs r1 -> r2 -> r2 -> r1 over angle, +da, +2da, +3da.

    gear1 = gear(1.0, 4.0, 1.0, 20, 0.7)  red    at (-3.0, -2.0)
    gear2 = gear(0.5, 2.0, 2.0, 10, 0.7)  green  at ( 3.1, -2.0)
    gear3 = gear(1.3, 2.0, 0.5, 10, 0.7)  blue   at (-3.1,  4.2)

    view_rotx = 20, view_roty = 30, view_rotz = 0
    gear2 is turned -9 degrees and gear3 -25 relative to gear1, which is
    the phase that makes the teeth mesh.

The view is orthographic here where glxgears' is perspective.  Over a
scene thirteen units wide seen from forty away the two differ by a few
percent, and an orthographic projection of a flat extrusion is an
affine map, which is what lets the C side draw a gear as one outline
swept along one vector instead of as a mesh.

The lighting is not carried over either: glxgears lights each face from
(5, 5, 10) and gets a continuous range of shades, and this desktop has
sixteen colours.  Each gear is two of them, the lit face and the sides,
which is what flat shading leaves you when the shades run out.
"""
import math
import sys
from pathlib import Path

# The sixteen, as tools/make-icons.py has them.
BLACK, BLUE, GREEN = 0x000000, 0x0000AA, 0x00AA00
RED, GREY, DARK = 0x9E1B1B, 0xAAAAAA, 0x555555
HIBLUE, HIGREEN, HIRED = 0x5555FF, 0x55FF55, 0xE8564B

UNIT = 1024             # view units are fixed point, 1.0 == UNIT
HUB_POINTS = 48         # the bore is a circle; this is how round it gets

ROTX, ROTY = math.radians(20.0), math.radians(30.0)
CX, SX = math.cos(ROTX), math.sin(ROTX)
CY, SY = math.cos(ROTY), math.sin(ROTY)

GEARS = (
    #  name    inner outer width teeth depth  at        phase  face    side
    ("red",    1.0,  4.0,  1.0,  20,   0.7,  (-3.0, -2.0),  0.0, HIRED,   RED),
    ("green",  0.5,  2.0,  2.0,  10,   0.7,  ( 3.1, -2.0),  -9.0, HIGREEN, GREEN),
    ("blue",   1.3,  2.0,  0.5,  10,   0.7,  (-3.1,  4.2), -25.0, HIBLUE,  BLUE),
)


def project(x, y, z):
    """Rx(20) . Ry(30) . p, then drop z and flip y for a screen.

    Screen y grows downward and the model's does not, which is the sign
    that has to be here rather than left for the caller to remember."""
    vx = CY * x + SY * z
    vy = SX * SY * x + CX * y - SX * CY * z
    depth = -CX * SY * x + SX * y + CX * CY * z
    return vx, -vy, depth


def fixed(value):
    return int(round(value * UNIT))


def outline(inner, outer, width, teeth, depth, at, phase):
    """The front face's outline, as gears.c walks it."""
    r1 = outer - depth / 2.0
    r2 = outer + depth / 2.0
    da = 2.0 * math.pi / teeth / 4.0
    turn = math.radians(phase)
    points = []
    for tooth in range(teeth):
        a = tooth * 2.0 * math.pi / teeth
        for radius, step in ((r1, 0), (r2, 1), (r2, 2), (r1, 3)):
            t = a + step * da + turn
            points.append((at[0] + radius * math.cos(t),
                           at[1] + radius * math.sin(t)))
    hub = []
    for i in range(HUB_POINTS):
        t = i * 2.0 * math.pi / HUB_POINTS + turn
        hub.append((at[0] + inner * math.cos(t),
                    at[1] + inner * math.sin(t)))
    return points, hub


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "src/trait_gears_art.h")

    shapes = []
    for name, inner, outer, width, teeth, depth, at, phase, face, side in GEARS:
        face_pts, hub_pts = outline(inner, outer, width, teeth, depth,
                                    at, phase)
        back, front = -width / 2.0, width / 2.0
        flat = []
        for x, y in face_pts:
            vx, vy, _ = project(x, y, back)
            flat.append((vx, vy))
        bore = []
        for x, y in hub_pts:
            vx, vy, _ = project(x, y, back)
            bore.append((vx, vy))
        b = project(at[0], at[1], back)
        f = project(at[0], at[1], front)
        shapes.append({
            "name": name, "face": face, "side": side,
            "outer": flat, "hub": bore,
            "sweep": (f[0] - b[0], f[1] - b[1]),
            # Back to front: the nearer gear is drawn last, so the order
            # this list is emitted in IS the painter's order.
            "depth": project(at[0], at[1], 0.0)[2],
        })
    shapes.sort(key=lambda s: s["depth"])

    every = [p for s in shapes for p in s["outer"]]
    every += [(p[0] + s["sweep"][0], p[1] + s["sweep"][1])
              for s in shapes for p in s["outer"]]
    min_x = min(p[0] for p in every)
    max_x = max(p[0] for p in every)
    min_y = min(p[1] for p in every)
    max_y = max(p[1] for p in every)

    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/*",
        " * GENERATED by tools/make-gears.py - do not edit.",
        " *",
        " * glxgears' three gears, projected.  Each is one closed outline",
        " * and one bore, in view units of 1/%d, on the plane behind the" % UNIT,
        " * gear; `sweep` is the vector from that plane to the lit face,",
        " * so the solid is the outline swept along it.  See",
        " * assets/gears/SOURCE.txt.",
        " */",
        "#ifndef TRAIT_GEARS_ART_H",
        "#define TRAIT_GEARS_ART_H",
        "",
        "#include <stdint.h>",
        "",
        "#define TRAIT_GEARS_UNIT %dL" % UNIT,
        "#define TRAIT_GEARS_COUNT %uU" % len(shapes),
        "",
        "/* The whole scene, so a caller can fit it to a screen. */",
        "#define TRAIT_GEARS_MIN_X %dL" % fixed(min_x),
        "#define TRAIT_GEARS_MAX_X %dL" % fixed(max_x),
        "#define TRAIT_GEARS_MIN_Y %dL" % fixed(min_y),
        "#define TRAIT_GEARS_MAX_Y %dL" % fixed(max_y),
        "",
    ]

    def emit(symbol, points):
        lines.append("static const int32_t %s[] = {" % symbol)
        row = []
        for x, y in points:
            row.append("%dL, %dL," % (fixed(x), fixed(y)))
            if len(row) == 4:
                lines.append("    " + " ".join(row))
                row = []
        if row:
            lines.append("    " + " ".join(row))
        lines.append("};")

    for s in shapes:
        lines.append("/* %s: %u outline points, %u in the bore */"
                     % (s["name"], len(s["outer"]), len(s["hub"])))
        emit("trait_gears_%s_outline" % s["name"], s["outer"])
        emit("trait_gears_%s_bore" % s["name"], s["hub"])
    lines += [
        "",
        "struct trait_gears_shape {",
        "    const int32_t *outline;   /* x, y interleaved, view units */",
        "    uint32_t outline_points;",
        "    const int32_t *bore;",
        "    uint32_t bore_points;",
        "    int32_t sweep_x;          /* back plane -> lit face */",
        "    int32_t sweep_y;",
        "    uint32_t face;            /* the lit face's colour */",
        "    uint32_t side;            /* everything the light misses */",
        "};",
        "",
        "/* Farthest first: this is the order they are painted in. */",
        "static const struct trait_gears_shape"
        " trait_gears[TRAIT_GEARS_COUNT] = {",
    ]
    for s in shapes:
        lines.append("    { trait_gears_%s_outline, %uU,"
                     % (s["name"], len(s["outer"])))
        lines.append("      trait_gears_%s_bore, %uU,"
                     % (s["name"], len(s["hub"])))
        lines.append("      %dL, %dL, 0x%06XU, 0x%06XU },"
                     % (fixed(s["sweep"][0]), fixed(s["sweep"][1]),
                        s["face"], s["side"]))
    lines += [
        "};",
        "",
        "#endif /* TRAIT_GEARS_ART_H */",
    ]
    out.write_text("\n".join(lines) + "\n")
    print("wrote %s: %s, scene %.2f x %.2f view units"
          % (out, ", ".join("%s %u+%u" % (s["name"], len(s["outer"]),
                                          len(s["hub"])) for s in shapes),
             max_x - min_x, max_y - min_y))


if __name__ == "__main__":
    main()
