#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate deterministic C planes from preserved OpenGAT panel artwork.

Each source image is cropped to its visible ink, resampled independently for
all requested sizes, and emitted as separate colour and alpha arrays. JPEG
inputs with flat backgrounds are keyed from the border inward. PNG alpha is
preserved.

    python3 tools/make-app-icons.py assets/opengat/panel \
        src/kernel/de_opengat_panel_art.h --prefix opengat_panel_art \
        --sizes 16,20,24
"""

import sys
from collections import deque
from pathlib import Path

from PIL import Image, ImageFilter

# The sizes anything draws this artwork at: a tray slot, a taskbar button, a
# Start tile, and the logo on a Store card.
SIZES = (16, 24, 32, 48)
# Artwork that arrives too pale to read on a light UI.
#
# This is a CONTRAST STRETCH and not a repaint. Pure white stays pure white,
# so a light figure on a light body keeps its edge, and everything below
# white falls this many times further from it. The artwork's own shading
# survives; only its distance from white changes. Drop an entry the moment a
# version arrives that does not need it.
#
# The Store's bag needed it while the mark on it was the same achromatic
# near-white as the bag: ink averaging 227 of 255 with a chroma under 2, a
# shape you had to already know was there on the light taskbar. The bag it
# carries now has OpenGAT's logo on it in the accent blue, which separates
# itself, and stretching that blue only drove it towards navy. So the entry
# is gone, and this table is empty until artwork arrives that needs it.
INK_CONTRAST = {}
# JPEG mottling across a flat area is invisible until a stretch multiplies it
# as well. Five pixels at the size these arrive is well under any real edge.
DENOISE_RADIUS = 5

# A little sharpening after the reduction. A 512-pixel icon resampled to 24
# is soft no matter how good the filter is; this is what an icon editor's
# "save for small size" does, and without it the result reads as blurred.
SHARPEN = (0.7, 70, 2)   # radius, percent, threshold
# How close to the corner colour counts as background.  Loose enough and the
# flood eats the artwork: the Store's bag is a pale grey on white and at 28 the
# fill walked straight through its lighter half, leaving a bar with a hole in
# it, and Task Manager's monitor lost its rim the same way.  Tight enough and
# JPEG noise in the background stops keying and comes through as ink instead.
# Twelve is between the two, checked against every icon in assets/icons/app.
BACKGROUND_TOLERANCE = 12


# At most this many distinct colours are learned from the border. A border
# that needs more than a handful is not a background — it is a gradient or a
# photograph — and the corner alone is the safer reading of it.
BACKGROUND_COLOURS = 8


def background_colours(image):
    """The distinct colours the border is made of.

    One reference is right for artwork sitting on a flat colour and wrong for
    artwork saved over a transparency CHECKERBOARD, which is two greys further
    apart than any tolerance that does not also eat pale artwork. Both of them
    are on the border, so both are learned from it: Task Manager's icon
    arrived checkered and the Store's did not, and this reads each correctly
    without either being told which it is.
    """
    width, height = image.size
    pixels = image.load()
    learned = []

    def note(colour):
        for reference in learned:
            if all(abs(a - b) <= BACKGROUND_TOLERANCE
                   for a, b in zip(colour, reference)):
                return
        if len(learned) < BACKGROUND_COLOURS:
            learned.append(colour)

    for x in range(width):
        note(pixels[x, 0][:3])
        note(pixels[x, height - 1][:3])
    for y in range(height):
        note(pixels[0, y][:3])
        note(pixels[width - 1, y][:3])
    if len(learned) >= BACKGROUND_COLOURS:
        return [pixels[0, 0][:3]]
    return learned


def key_out_background(image):
    """Make the flat colour around the artwork transparent.

    Flood filling inwards from the border rather than testing every pixel is
    what keeps an enclosed shape of the same colour — the white bag inside the
    purple disc — from being erased along with the background it matches.
    """
    if image.getchannel("A").getextrema()[0] < 255:
        return image           # it already has an alpha channel worth keeping
    width, height = image.size
    pixels = image.load()
    references = background_colours(image)
    seen = bytearray(width * height)
    queue = deque()
    for x in range(width):
        queue.append((x, 0))
        queue.append((x, height - 1))
    for y in range(height):
        queue.append((0, y))
        queue.append((width - 1, y))
    while queue:
        x, y = queue.popleft()
        if x < 0 or y < 0 or x >= width or y >= height:
            continue
        if seen[y * width + x]:
            continue
        red, green, blue, _ = pixels[x, y]
        if not any(abs(red - r) <= BACKGROUND_TOLERANCE and
                   abs(green - g) <= BACKGROUND_TOLERANCE and
                   abs(blue - b) <= BACKGROUND_TOLERANCE
                   for r, g, b in references):
            continue
        seen[y * width + x] = 1
        pixels[x, y] = (red, green, blue, 0)
        queue.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    return image


def crop_to_ink(image):
    """Trim the empty margin, then pad back to a square around the artwork."""
    box = image.getchannel("A").getbbox()
    if box is None:
        return image
    cropped = image.crop(box)
    side = max(cropped.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(cropped, ((side - cropped.size[0]) // 2,
                           (side - cropped.size[1]) // 2))
    return square


def raise_contrast(image, factor):
    """Push the artwork away from white by `factor`, anchored at white.

    A plain multiply would darken the white glyph along with the body it sits
    on and cost the logo its figure. Anchoring at white keeps 255 at 255 and
    moves everything else, which is the difference between darkening a
    picture and flattening one.
    """
    if factor == 1.0:
        return image
    smooth = image.convert("RGB").filter(
        ImageFilter.MedianFilter(DENOISE_RADIUS))
    smooth = smooth.convert("RGBA")
    smooth.putalpha(image.getchannel("A"))
    pixels = smooth.load()
    width, height = smooth.size
    for y in range(height):
        for x in range(width):
            red, green, blue, opacity = pixels[x, y]
            if opacity == 0:
                continue
            pixels[x, y] = (
                max(0, 255 - int((255 - red) * factor)),
                max(0, 255 - int((255 - green) * factor)),
                max(0, 255 - int((255 - blue) * factor)),
                opacity)
    return smooth


def reduce_to(image, size):
    """Resample with the alpha premultiplied, so edges do not pick up haloes.

    Resampling colour and alpha separately averages the colour of pixels that
    were fully transparent into the ones that were not, which puts a rim of
    whatever the empty area happened to be around every edge. Premultiplying
    first is the standard answer and Pillow does it on request.
    """
    small = image.convert("RGBa").resize((size, size), Image.LANCZOS) \
        .convert("RGBA")
    return small.filter(ImageFilter.UnsharpMask(*SHARPEN))


def emit(lines, prefix, name, image, size, pack):
    pixels = image.load()
    lines.append(f"static const uint32_t {prefix}_{name}_{size}_pixels"
                 f"[{size} * {size}] = {{")
    for y in range(size):
        row = []
        for x in range(size):
            red, green, blue, _ = pixels[x, y]
            row.append(f"0x{pack(red, green, blue):08X}U,")
        for start in range(0, size, 8):
            lines.append("    " + " ".join(row[start:start + 8]))
    lines.append("};")
    lines.append(f"static const uint8_t {prefix}_{name}_{size}_alpha"
                 f"[{size} * {size}] = {{")
    for y in range(size):
        row = [f"{pixels[x, y][3]:3d}," for x in range(size)]
        for start in range(0, size, 16):
            lines.append("    " + " ".join(row[start:start + 16]))
    lines.append("};")


def build(source, target, prefix, sizes, only):
    """One generated header: every named icon, at every requested size."""
    upper = prefix.upper()
    artwork = {}
    for path in source.iterdir():
        if path.suffix.lower() not in (".png", ".jpg", ".jpeg"):
            continue
        if path.stem in artwork:
            raise SystemExit(
                f"duplicate artwork stem in {source}: "
                f"{artwork[path.stem].name}, {path.name}"
            )
        artwork[path.stem] = path
    names = sorted(artwork)
    if only:
        missing = [name for name in only if name not in names]
        if missing:
            raise SystemExit(f"not in {source}: {', '.join(missing)}")
        names = [name for name in names if name in only]
    if not names:
        raise SystemExit(f"no artwork in {source}")

    # OpenGAT's framebuffer reports channel positions at boot; QEMU's adaptors
    # report 0x00RRGGBB and the preview pins that, so the planes are baked in
    # that order and the caller recomposes if a device ever differs.
    def pack(red, green, blue):
        return (red << 16) | (green << 8) | blue

    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/*",
        " * GENERATED by tools/make-app-icons.py - do not edit.",
        " *",
        " * Application artwork, as the colour plane and separate alpha plane",
        " * the shell's icon tables take.  One pair per size it draws at,",
        " * each resampled from the original rather than from a larger cell,",
        " * because a picture reduced twice is a picture blurred twice.",
        " *",
        " * Channels are packed 0x00RRGGBB.  OpenGAT learns its real channel",
        " * positions from the loader at boot; the drawing code repacks these",
        " * if the device it finds disagrees.",
        " */",
        f"#ifndef {upper}_H",
        f"#define {upper}_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        f"#define {upper}_SIZES {len(sizes)}U",
        "",
        f"static const uint32_t {prefix}_size[{upper}_SIZES] = {{",
        "    " + ", ".join(f"{size}U" for size in sizes),
        "};",
        "",
    ]

    for name in names:
        path = artwork[name]
        image = crop_to_ink(key_out_background(
            Image.open(path).convert("RGBA")))
        image = raise_contrast(image, INK_CONTRAST.get(name, 1.0))
        identifier = name.replace("-", "_")
        lines.append(f"/* {path.name}, {image.size[0]} square after cropping */")
        for size in sizes:
            emit(lines, prefix, identifier, reduce_to(image, size), size, pack)
        lines.append("")
        print(f"  {name}: {path.name} {image.size[0]}px -> {sizes}")

    lines.append(f"struct {prefix}_entry {{")
    lines.append("    const char *name;")
    lines.append(f"    const uint32_t *pixels[{upper}_SIZES];")
    lines.append(f"    const uint8_t *alpha[{upper}_SIZES];")
    lines.append("};")
    lines.append("")
    lines.append(f"static const struct {prefix}_entry {prefix}[] = {{")
    for name in names:
        identifier = name.replace("-", "_")
        planes = ", ".join(f"{prefix}_{identifier}_{size}_pixels"
                           for size in sizes)
        alphas = ", ".join(f"{prefix}_{identifier}_{size}_alpha"
                           for size in sizes)
        lines.append(f'    {{ "{name}", {{ {planes} }}, {{ {alphas} }} }},')
    lines.append("};")
    lines.append("")
    lines.append(f"#define {upper}_COUNT "
                 f"(sizeof({prefix}) / sizeof({prefix}[0]))")
    lines.append("")
    lines.append("#endif")
    target.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    print(f"wrote {target} with {len(names)} icons at {sizes}")


def main():
    positional = []
    options = {}
    argv = sys.argv[1:]
    index = 0
    while index < len(argv):
        if argv[index].startswith("--"):
            options[argv[index][2:]] = argv[index + 1]
            index += 2
            continue
        positional.append(argv[index])
        index += 1
    source = Path(positional[0] if positional else "assets/icons/app")
    target = Path(positional[1] if len(positional) > 1
                  else "src/kernel/taskbar_art.h")
    sizes = tuple(int(v) for v in options["sizes"].split(",")) \
        if "sizes" in options else SIZES
    only = [v for v in options.get("only", "").split(",") if v]
    build(source, target, options.get("prefix", "taskbar_art"), sizes, only)


if __name__ == "__main__":
    main()
