#!/usr/bin/env python3
"""Assemble the frames build/film wrote into one animated GIF.

Not ffmpeg. The ffmpeg that ships with the browser in this environment
is built with the image2 muxer and nothing else - no mp4, no gif - so
the frames are muxed here instead.

The palette is not guessed at. A console frame contains exactly the
sixteen colours tools/render.c paints with and nothing in between: the
glyphs are one bit deep, so there is no antialiasing and no blending.
Reading that table out of render.c and mapping every frame onto it
gives an exact GIF and a small one.

The first attempt quantised each frame adaptively and then forced them
all onto the first frame's palette. Frame one is a grey box on a red
field and has no idea what colour the rest of the film is, so a screen
later on came out olive.

Durations come from the file the C program wrote beside the frames, so
a pause is one frame held rather than the same picture written out
twenty times.
"""
import re
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent.parent
FILM = HERE / "screens" / "film"
OUT = HERE / "screens" / "install.gif"
RENDER = HERE / "tools" / "render.c"


def console_palette():
    """The sixteen colours, read from the renderer that paints them."""
    text = RENDER.read_text()
    block = text[text.index("PALETTE[16][3]"):]
    block = block[:block.index("};")]
    found = re.findall(r"\{\s*0x([0-9A-Fa-f]{2}),\s*0x([0-9A-Fa-f]{2}),"
                       r"\s*0x([0-9A-Fa-f]{2})\s*\}", block)
    if len(found) != 16:
        sys.exit("expected 16 colours in render.c, found %d" % len(found))
    return [tuple(int(c, 16) for c in rgb) for rgb in found]


def main():
    frames = sorted(FILM.glob("f*.png"))
    if not frames:
        sys.exit("no frames in %s - run build/film first" % FILM)

    timing = (FILM / "timing.txt").read_text().split()
    if len(timing) != len(frames):
        sys.exit("%d frames but %d durations" % (len(frames), len(timing)))
    durations = [int(t) for t in timing]

    colours = console_palette()
    flat = [v for rgb in colours for v in rgb]
    reference = Image.new("P", (1, 1))
    reference.putpalette(flat + flat[:3] * (256 - len(colours)))

    images = []
    stray = set()
    for path in frames:
        rgb = Image.open(path).convert("RGB")
        # Every colour in the frame has to be one the renderer paints.
        # If it is not, the renderer has grown something this palette
        # does not know about and the GIF would quietly approximate it.
        stray |= {c for _, c in rgb.getcolors(maxcolors=4096)
                  if c not in colours}
        images.append(rgb.quantize(palette=reference,
                                   dither=Image.Dither.NONE))
    if stray:
        sys.exit("REFUSED: %d colour(s) no palette entry matches, e.g. %s"
                 % (len(stray), sorted(stray)[:3]))

    images[0].save(OUT, save_all=True, append_images=images[1:],
                   duration=durations, loop=0, optimize=True, disposal=1)

    seconds = sum(durations) / 1000.0
    print("%s  %d frames, %.1fs, %dx%d, %.1f MiB, %d colours"
          % (OUT.relative_to(HERE), len(images), seconds,
             images[0].width, images[0].height,
             OUT.stat().st_size / (1024 * 1024), len(colours)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
