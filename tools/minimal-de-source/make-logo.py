#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Turn the captured colour ASCII of the OpenRFS mark into src/trait_logo.h.

    python3 tools/make-logo.py [art.json] [out.h]

WHERE THE ART COMES FROM, AND WHY IT IS CAPTURED RATHER THAN COMPUTED.

assets/logo/openrfs-logo-2026.json is coddy.tech's ASCII art generator
run over the drawing, in image mode, with Color on, Invert off and the
width slider at exactly 45.  It is captured whole - every character and
the colour of every character - because that generator does two things
at once that this project had been doing in two places and badly:

  * it picks a CHARACTER per cell out of a gradient, so an edge running
    diagonally through a cell reads as an edge rather than as a block;
  * it picks a COLOUR per cell out of the source pixels, so the shading
    on the fish survives instead of being flattened to two reds.

The previous pipeline took its characters from asciiart.eu's
"Black and White" set - which, read out of that page's own source, is
the single character U+2588 and a space: a silhouette, thresholded at
pure white.  Everything that made the mark a face had to be put back
afterwards by classifying the drawing into five things and inking each
cell by a vote.  This does not need any of that, and none of it is left.

WHAT THIS FILE STILL DOES.  The capture holds 251 distinct colours over
442 inked cells, nearly all of them reds a shade apart.  A terminal cell
here carries one byte of attribute, so the colours are reduced to a
palette of at most fifteen by k-means over the cells that use them, and
each cell keeps the index of its nearest entry.  Index 0 is not a colour:
it means the terminal's own foreground, which is what a cell with
nothing drawn in it gets.
"""
import json
import re
import sys
from pathlib import Path

PALETTE_MAX = 15     # 1..15, because 0 means "the terminal's own ink"
MAX_LINE = 64        # a fetch has to leave room for the facts beside it


def rgb(colour):
    """CSS rgb(r, g, b) as a triple. The capture holds computed style."""
    parts = re.findall(r"\d+", colour or "")
    if len(parts) < 3:
        return (255, 255, 255)
    return tuple(int(value) for value in parts[:3])


def kmeans(points, wanted, rounds=40):
    """Lloyd's algorithm, seeded evenly along the sorted points.

    Deterministic on purpose: a generated header that comes out
    different every time it is regenerated is a diff nobody can read.
    """
    if not points:
        return []
    unique = sorted(set(points))
    if len(unique) <= wanted:
        return unique
    step = len(unique) / float(wanted)
    centres = [unique[min(len(unique) - 1, int(i * step))]
               for i in range(wanted)]
    for _ in range(rounds):
        groups = [[] for _ in centres]
        for point in points:
            best = min(range(len(centres)),
                       key=lambda i: sum((point[c] - centres[i][c]) ** 2
                                         for c in range(3)))
            groups[best].append(point)
        moved = []
        for centre, group in zip(centres, groups):
            if not group:
                moved.append(centre)
                continue
            moved.append(tuple(sum(p[c] for p in group) // len(group)
                               for c in range(3)))
        if moved == centres:
            break
        centres = moved
    return sorted(centres)


def nearest(colour, palette):
    return min(range(len(palette)),
               key=lambda i: sum((colour[c] - palette[i][c]) ** 2
                                 for c in range(3)))


def main():
    art = Path(sys.argv[1] if len(sys.argv) > 1
               else "assets/logo/openrfs-logo-2026.json")
    out = Path(sys.argv[2] if len(sys.argv) > 2 else "src/trait_logo.h")

    rows = json.loads(art.read_text())
    while rows and not any(ch != " " for ch, _ in rows[0]):
        rows.pop(0)
    while rows and not any(ch != " " for ch, _ in rows[-1]):
        rows.pop()
    if not rows:
        sys.exit("REFUSED: %s has no art in it" % art.name)

    columns = max(len(row) for row in rows)
    if columns > MAX_LINE:
        sys.exit("REFUSED: %d columns leaves no room for the facts"
                 % columns)
    for row in rows:
        for ch, _ in row:
            if not (" " <= ch <= "~"):
                sys.exit("REFUSED: %r is not a character this font has"
                         % ch)

    inked = [rgb(colour) for row in rows for ch, colour in row
             if ch != " "]
    if not inked:
        sys.exit("REFUSED: nothing in %s is inked" % art.name)
    palette = kmeans(inked, PALETTE_MAX)

    lines = []
    codes = []
    for row in rows:
        text = "".join(ch for ch, _ in row).rstrip()
        ink = []
        for column, (ch, colour) in enumerate(row):
            if column >= len(text):
                break
            ink.append(0 if ch == " "
                       else 1 + nearest(rgb(colour), palette))
        lines.append(text)
        codes.append(ink)

    used = {code for ink in codes for code in ink if code}
    if len(used) < 3:
        sys.exit("REFUSED: the whole mark came out %d colours - the "
                 "capture lost its shading" % len(used))

    text = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/*",
        " * GENERATED by tools/make-logo.py - do not edit.",
        " *",
        " * The characters AND their colours are %s," % art.name,
        " * which is coddy.tech's ASCII art generator run over the",
        " * drawing with Color on and the width at 45.  Nothing here is",
        " * classified or averaged: every cell is the cell that",
        " * generator produced, with its colour snapped to the nearest",
        " * of %d.  assets/logo/SOURCE.txt records the settings and" % len(palette),
        " * whose drawing it is.",
        " */",
        "#ifndef TRAIT_LOGO_H",
        "#define TRAIT_LOGO_H",
        "",
        "#define TRAIT_LOGO_ROWS %uU" % len(lines),
        "#define TRAIT_LOGO_COLUMNS %uU" % columns,
        "#define TRAIT_LOGO_INKS %uU" % (len(palette) + 1),
        "",
        "/*",
        " * THE MARK'S OWN COLOURS, reduced from the %d the capture"
        % len(set(inked)),
        " * holds to %d by k-means over the cells that use them."
        % len(palette),
        " * Entry 0 is not a colour: a cell with nothing drawn in it",
        " * takes the terminal's own foreground.",
        " */",
        "static const uint32_t trait_logo_ink[TRAIT_LOGO_INKS] = {",
        "    0x000000U,   /* unused: index 0 means the terminal's ink */",
    ]
    for colour in palette:
        text.append("    0x%02X%02X%02XU," % colour)
    text += [
        "};",
        "",
        "static const char *const trait_logo[TRAIT_LOGO_ROWS] = {",
    ]
    for line in lines:
        text.append('    "%s",' % line.replace("\\", "\\\\")
                                      .replace('"', '\\"'))
    text += [
        "};",
        "",
        "/* One hex digit per character of the row above: an index into",
        " * trait_logo_ink, and 0 where nothing is drawn. */",
        "static const char *const trait_logo_at[TRAIT_LOGO_ROWS] = {",
    ]
    for ink in codes:
        text.append('    "%s",' % "".join("%X" % code for code in ink))
    text += ["};", "", "#endif /* TRAIT_LOGO_H */"]
    out.write_text("\n".join(text) + "\n")
    print("wrote %s: %u rows, widest %u characters, %u colours from %u"
          % (out, len(lines), columns, len(palette), len(set(inked))))


if __name__ == "__main__":
    main()
