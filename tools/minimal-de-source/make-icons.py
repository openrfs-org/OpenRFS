#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Unpack the vendored XPMs into the table the file manager draws from.

    python3 tools/make-icons.py [dir] [out.h]

NOTHING IS DRAWN HERE.  An earlier version of this file drew the icons
from geometry - flat boxes in the sixteen colours - and they came out
looking like a style rather than like icons.  These are Johan Hanson's,
shipped with the gentoo file manager in 1998, copied byte for byte into
assets/icons/gentoo and read back out here.  See the SOURCE.txt beside
them.

There is ONE SIZE, because there is one size: 16 by 15, drawn a pixel at
a time for a 1998 screen.  Scaling it up to 48 for an icon view would be
a picture of an icon rather than an icon, so the icon view draws the 16
as well and the list view - which is what gentoo itself was - is what a
Files window opens in.

The cell is padded to 16 by 16 so the table is square.  The pad is a row
of transparent pixels at the foot; not one pixel of the drawing moves.
"""
import gzip
import re
import sys
from pathlib import Path

from PIL import Image

SIZE = 16

# What the file manager asks for, and which of the vendored files
# answers.  Two names can share a file: a home folder IS a folder, and
# gentoo drew two of them rather than a special one.
NAMES = {
    "folder": "Directory",
    "user-home": "Directory2",
    "user-desktop": "Directory",
    "drive-harddisk": "Harddrive",
    "text-x-generic": "txt",
    "image-x-generic": "Image",
    "audio-x-generic": "Speaker",
    "application-x-executable": "Executable",
}

# The X11 colour names these files actually use, and nothing else: a
# table of every name X ships would be a table mostly of guesses.
X11 = {
    "black": (0, 0, 0), "white": (255, 255, 255),
    "red": (255, 0, 0), "green": (0, 255, 0), "blue": (0, 0, 255),
    "yellow": (255, 255, 0), "cyan": (0, 255, 255),
    "magenta": (255, 0, 255),
    "gray": (190, 190, 190), "grey": (190, 190, 190),
    "none": None,
}


def colour(text):
    text = text.strip()
    low = text.lower()
    if low in X11:
        return X11[low]
    if text.startswith("#"):
        digits = text[1:]
        if len(digits) == 12:            # #RRRRGGGGBBBB
            digits = digits[0:2] + digits[4:6] + digits[8:10]
        elif len(digits) == 3:
            digits = "".join(c * 2 for c in digits)
        elif len(digits) != 6:
            raise ValueError("cannot read colour %r" % text)
        return tuple(int(digits[at:at + 2], 16) for at in (0, 2, 4))
    match = re.match(r"^(?:gray|grey)(\d+)$", low)
    if match:
        level = int(int(match.group(1)) * 255 / 100)
        return (level, level, level)
    raise ValueError("cannot read colour %r" % text)


def read_xpm(path):
    """XPM3 is a C array of strings, so this reads the strings."""
    raw = path.read_bytes()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    rows = re.findall(r'"((?:[^"\\]|\\.)*)"', raw.decode("latin-1"))
    if not rows:
        raise ValueError("%s carries no XPM strings" % path.name)
    width, height, count, per = (int(v) for v in rows[0].split()[:4])
    table = {}
    for line in rows[1:1 + count]:
        key, rest = line[:per], line[per:].split()
        value = None
        for at, token in enumerate(rest):
            # `c` is the colour visual; `m`, `g` and `s` are the mono,
            # greyscale and symbolic ones, and a file may give any of
            # them.  Colour wins where it is there.
            if token == "c" and at + 1 < len(rest):
                value = " ".join(rest[at + 1:])
                break
            if token in ("m", "g", "g4", "s") and at + 1 < len(rest) \
                    and value is None:
                value = rest[at + 1]
        table[key] = colour(value if value else "none")
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    pixels = image.load()
    for y, line in enumerate(rows[1 + count:1 + count + height]):
        for x in range(width):
            found = table.get(line[x * per:(x + 1) * per])
            if found is not None:
                pixels[x, y] = (found[0], found[1], found[2], 255)
    return image


def main():
    art = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/icons/gentoo")
    out = Path(sys.argv[2] if len(sys.argv) > 2
               else "src/trait_files_art.h")

    icons = {}
    for name, source in sorted(NAMES.items()):
        image = read_xpm(art / (source + ".xpm"))
        if image.width > SIZE or image.height > SIZE:
            sys.exit("REFUSED: %s is %ux%u, larger than the %u cell"
                     % (source, image.width, image.height, SIZE))
        cell = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
        cell.paste(image, (0, 0))
        if cell.getchannel("A").getbbox() is None:
            sys.exit("REFUSED: %s drew nothing" % source)
        icons[name] = (source, image.size, list(cell.get_flattened_data()))

    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/*",
        " * GENERATED by tools/make-icons.py - do not edit.",
        " *",
        " * The gentoo file manager's icons, Johan Hanson 1998, unpacked",
        " * from the XPMs vendored under assets/icons/gentoo.  Nothing",
        " * here was redrawn and nothing was resampled: each is the",
        " * file's own 16x15, padded with a transparent row to square",
        " * the cell.  See assets/icons/gentoo/SOURCE.txt.",
        " *",
        " * Channels are packed 0x00RRGGBB with a separate alpha plane,",
        " * because a framebuffer has no alpha channel to composite in.",
        " */",
        "#ifndef TRAIT_FILES_ART_H",
        "#define TRAIT_FILES_ART_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "#define TRAIT_FILES_ART_SIZES 1U",
        "",
        "static const uint32_t trait_files_art_size"
        "[TRAIT_FILES_ART_SIZES] = {",
        "    %uU" % SIZE,
        "};",
        "",
    ]
    for name, (source, size, data) in icons.items():
        ident = name.replace("-", "_")
        lines.append("/* %s: %s.xpm, %ux%u */"
                     % (name, source, size[0], size[1]))
        for plane, kind, width in (("pixels", "uint32_t", 8),
                                   ("alpha", "uint8_t", 16)):
            lines.append("static const %s trait_files_art_%s_%u_%s"
                         "[%u * %u] = {"
                         % (kind, ident, SIZE, plane, SIZE, SIZE))
            row = []
            for r, g, b, a in data:
                row.append("0x00%02X%02X%02XU," % (r, g, b)
                           if plane == "pixels" else "%4d," % a)
                if len(row) == width:
                    lines.append("    " + " ".join(row))
                    row = []
            if row:
                lines.append("    " + " ".join(row))
            lines.append("};")
        lines.append("")

    lines += [
        "struct trait_files_art_entry {",
        "    const char *name;",
        "    const uint32_t *pixels[TRAIT_FILES_ART_SIZES];",
        "    const uint8_t *alpha[TRAIT_FILES_ART_SIZES];",
        "};",
        "",
        "static const struct trait_files_art_entry trait_files_art[] = {",
    ]
    for name in icons:
        ident = name.replace("-", "_")
        lines.append('    { "%s", { trait_files_art_%s_%u_pixels }, '
                     "{ trait_files_art_%s_%u_alpha } },"
                     % (name, ident, SIZE, ident, SIZE))
    lines += [
        "};",
        "",
        "#define TRAIT_FILES_ART_COUNT "
        "(sizeof(trait_files_art) / sizeof(trait_files_art[0]))",
        "",
        "#endif",
    ]
    out.write_text("\n".join(lines) + "\n")
    print("wrote %s: %u icons from %u files in %s"
          % (out, len(icons), len({s for s, _, _ in icons.values()}), art))


if __name__ == "__main__":
    main()
