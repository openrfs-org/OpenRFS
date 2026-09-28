#!/usr/bin/env python3
"""The console font: the VGA character ROM, as FreeBSD ships it.

There is no font engine behind a framebuffer, so the glyphs are turned
into bitmaps here and the console just copies bits.

WHY THIS STOPPED BEING A TRUETYPE FACE.

It was DejaVu Sans Mono at 15 pixels, rasterised into a 9x18 cell and
thresholded at 110 - one bit per pixel, no anti-aliasing, because a text
console has none. That is a reasonable way to get a console font and it
produces a bad one: an outline face is drawn for a rasteriser that can
put grey on the edge of a stem, and when you take the grey away what is
left is a stem that is two pixels wide in one letter and one in the
next, with serifs and crossbars that come and go depending on where the
curve happened to fall relative to the pixel grid. It read as a font
that had been damaged, because it had been.

The answer is not a better threshold. It is a font that was DRAWN at one
bit per pixel for an 8x16 cell, and the one a PC console actually uses
is the character generator in the VGA ROM - the glyphs the BIOS puts on
the screen before any operating system loads, and the ones a BSD console
keeps using afterwards. Every stroke in it is a clean run of pixels
because there was never anything else it could be.

WHERE IT COMES FROM. share/vt/fonts/vgarom-8x16.hex in FreeBSD's own
tree: the font its console driver draws in, vendored into
assets/fonts/ byte for byte, with its sha256 in
assets/fonts/SOURCE.txt. Unifont hex format, which is one line per
glyph: the code point, a colon, and the rows as hex.

THE WRECKAGE RANGE IS GONE WITH THE PANIC SCREEN. 0x8F to 0xBF held the
upper half of the character set, which the panic screen filled the space
it was not using with - that being what is actually in video memory when
a text console falls over. There is no panic screen any more, so there
is nothing that draws them, and a font table carrying forty-nine glyphs
nothing can print is forty-nine glyphs of dead weight.

WHAT ELSE CAME WITH IT.

The line-drawing glyphs used to be computed here, because a frame is
built by laying the same cell next to itself eighty times and a
thresholded typeface does not promise that its line leaves one cell
exactly where it enters the next. The ROM promises it: CP437's box
characters are in there, and tiling is the whole reason they exist. So
nothing is drawn here any more - the frames, the shades, the block and
the arrows are all the ROM's own.

AND THERE IS NO BOLD, because a VGA text console has one character
generator. What the intensity bit does is change the colour, and
tools/render.c already has the bright half of the palette for that. A
second, heavier font table was a thing this console never had.
"""
import hashlib
import sys

HEX = "../../assets/fonts/vgarom-8x16.hex"
CELL_W, CELL_H = 8, 16
FIRST, LAST = 0x20, 0x8E

# Our code points, and the Unicode the ROM holds them under. Every one
# of these is a CP437 character; include/rsd/ui.h names them and no C
# file is allowed to write the number.
LINES = {
    0x80: ("hline", 0x2500),
    0x81: ("vline", 0x2502),
    0x82: ("ul",    0x250C),
    0x83: ("ur",    0x2510),
    0x84: ("ll",    0x2514),
    0x85: ("lr",    0x2518),
    0x86: ("ltee",  0x251C),
    0x87: ("rtee",  0x2524),
    0x88: ("ttee",  0x252C),
    0x89: ("btee",  0x2534),
    0x8A: ("cross", 0x253C),
}
# Two solids, for the gauge and for a textured ground, and two arrows.
SOLIDS = {0x8B: ("shade", 0x2591), 0x8C: ("block", 0x2588)}
ARROWS = {0x8D: ("up", 0x2191), 0x8E: ("down", 0x2193)}

def read_hex(path):
    """Unifont hex: CODEPOINT:ROWS, one glyph per line.

    The rows are most-significant-bit-first, which is the order a VGA
    character generator shifts them out in; this console indexes bit 0
    as the leftmost pixel, so each row is reversed on the way in. Get
    that backwards and every letter comes out mirrored, which is at
    least a mistake you can see.
    """
    glyphs = {}
    for line in open(path, encoding="ascii"):
        line = line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        code, bits = line.split(":", 1)
        if len(bits) != CELL_H * 2:
            raise SystemExit("%s: %s is not %d rows of 8 bits"
                             % (path, code, CELL_H))
        rows = []
        for y in range(CELL_H):
            byte = int(bits[y * 2:y * 2 + 2], 16)
            out = 0
            for x in range(CELL_W):
                if byte & (0x80 >> x):
                    out |= 1 << x
            rows.append(out)
        glyphs[int(code, 16)] = rows
    return glyphs


def blank():
    return [0 for _ in range(CELL_H)]


def build(rom):
    rows = []
    for code in range(FIRST, LAST + 1):
        want = None
        if code <= 0x7E:
            want = code
        elif code in LINES:
            want = LINES[code][1]
        elif code in SOLIDS:
            want = SOLIDS[code][1]
        elif code in ARROWS:
            want = ARROWS[code][1]
        if want is None:
            rows.append(blank())
        elif want not in rom:
            raise SystemExit("the ROM has no U+%04X, wanted for 0x%02X"
                             % (want, code))
        else:
            rows.append(rom[want])
    return rows


def label(code):
    if code == 0x20:
        return "space"
    if code <= 0x7E:
        return chr(code)
    if code in LINES:
        return LINES[code][0]
    if code in SOLIDS:
        return SOLIDS[code][0]
    if code in ARROWS:
        return ARROWS[code][0]
    return "unassigned"


def emit(out, name, rows):
    out.write("static const uint8_t %s[%d][%d] = {\n"
              % (name, LAST - FIRST + 1, CELL_H))
    for i, glyph in enumerate(rows):
        out.write("    { " + ", ".join("0x%02X" % b for b in glyph)
                  + " },   /* %s */\n" % label(FIRST + i))
    out.write("};\n\n")


def check(rows):
    """Every code point we named has ink, and the frames still tile.

    The tiling test is the one that matters and it is the reason these
    glyphs used to be drawn by hand: lay a horizontal next to itself and
    the run has to be continuous across the seam, or a frame eighty
    cells wide has seventy-nine gaps in it. The ROM's own box characters
    pass it - that is what they were cut for - but it is checked rather
    than assumed, because the day somebody points this tool at a
    prettier font is the day it stops being true.
    """
    problems = []
    for i, glyph in enumerate(rows):
        code = FIRST + i

        # Space has no ink by definition, and 0x7F is the hole between
        # ASCII and the high range - the table is indexed by code point,
        # so it has to hold a slot for it.
        if code in (0x20, 0x7F) or label(code) == "unassigned":
            continue
        if not any(glyph):
            problems.append("0x%02X (%s) is blank" % (code, label(code)))

    def arm(code, side):
        g = rows[code - FIRST]
        if side == "W":
            return any(row & 1 for row in g)
        if side == "E":
            return any(row & (1 << (CELL_W - 1)) for row in g)
        if side == "N":
            return g[0] != 0
        return g[CELL_H - 1] != 0

    # A horizontal must reach both side edges, a vertical both ends,
    # and every tee and corner must keep the arms its name promises.
    ARMS = {
        0x80: "WE", 0x81: "NS", 0x82: "SE", 0x83: "SW", 0x84: "NE",
        0x85: "NW", 0x86: "NSE", 0x87: "NSW", 0x88: "WES", 0x89: "WEN",
        0x8A: "NSEW",
    }
    for code, arms in ARMS.items():
        for side in arms:
            if not arm(code, side):
                problems.append("0x%02X (%s) loses its %s arm"
                                % (code, label(code), side))
        for side in "NSEW":
            if side not in arms and arm(code, side):
                problems.append("0x%02X (%s) has an %s arm it should not"
                                % (code, label(code), side))
    return problems


def main():
    digest = hashlib.sha256(open(HEX, "rb").read()).hexdigest()
    rom = read_hex(HEX)
    rows = build(rom)
    lit = sum(bin(b).count("1") for g in rows for b in g)

    problems = check(rows)
    if problems:
        for p in problems:
            print("REFUSED: " + p)
        return 1

    with open("../src/rsd_font.h", "w") as out:
        out.write("/* SPDX-License-Identifier: GPL-3.0-only */\n")
        out.write("/* GENERATED by tools/make-font.py - do not edit.\n"
                  " *\n"
                  " * THE VGA CHARACTER ROM, %dx%d, out of FreeBSD's own\n"
                  " * share/vt/fonts/vgarom-8x16.hex - the font its\n"
                  " * console driver draws in, which is the font the BIOS\n"
                  " * was already drawing in before it loaded.\n"
                  " *\n"
                  " * 0x20-0x7E ASCII, 0x80-0x8A CP437's box characters,\n"
                  " * 0x8B-0x8C its shade and block, 0x8D-0x8E its arrows.\n"
                  " *\n"
                  " * Nothing here is drawn by this project and nothing is\n"
                  " * thresholded out of an outline face: every glyph was\n"
                  " * cut at one bit per pixel for this cell.\n"
                  " *\n"
                  " * source sha256 %s\n */\n" % (CELL_W, CELL_H, digest))
        out.write("#ifndef RSD_FONT_H\n#define RSD_FONT_H\n\n")
        out.write("#include <stdint.h>\n\n")
        out.write("#define RSD_CELL_W %d\n#define RSD_CELL_H %d\n"
                  % (CELL_W, CELL_H))
        out.write("#define RSD_GLYPH_FIRST 0x%02X\n" % FIRST)
        out.write("#define RSD_GLYPH_LAST 0x%02X\n\n" % LAST)
        emit(out, "rsd_font", rows)
        out.write("#endif /* RSD_FONT_H */\n")

    print("wrote ../src/rsd_font.h  %d glyphs at %dx%d, %d lit pixels"
          % (len(rows), CELL_W, CELL_H, lit))
    print("sha256 %s  (assets/fonts/SOURCE.txt records where it is from)"
          % digest[:16])
    print("frames tile: every arm reaches its edge, and no glyph grew one")
    return 0


if __name__ == "__main__":
    sys.exit(main())
