#!/usr/bin/env python3
"""Draw the desktop wallpaper.

    python3 tools/make-wallpaper.py

Generated rather than sourced, for the same reason everything else here
is: a wallpaper that arrives as a file nobody can regenerate is a
wallpaper nobody can change the resolution of, and it is one more asset
whose provenance somebody has to take on trust.

The composition is this project's own. An ocean world sits low in the
frame, with deep space above it and the RSD wordmark over the limb.

The wordmark comes from assets/logo/rsd-mark.png.

Everything is deterministic. The same seed gives the same sky, so the
file in the repository can be rebuilt byte for byte.
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "wallpaper"
MARK = ROOT / "assets" / "logo" / "rsd-mark.png"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

W, H = 2560, 1440
SEED = 0x09E3779B          # a constant, not a choice

# The brand's two. The red is the mark's own; the blue is the water.
RED = (0x9E, 0x1B, 0x1B)
SEA = (0x10, 0x3C, 0xC8)
SEA_DEEP = (0x04, 0x0E, 0x4A)


def rng(state):
    """One LCG, so the sky is the same sky every time."""
    while True:
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        yield state


def starfield(img, seed):
    """Stars, with magnitudes rather than one brightness.

    A field of identical dots reads as noise. Real sky is mostly faint
    with a few bright ones, so the brightness is the cube of a uniform
    roll - which puts most stars near nothing and a handful near white.
    """
    px = img.load()
    r = rng(seed)
    for _ in range(14000):
        x = next(r) % W
        y = next(r) % H
        t = (next(r) % 1000) / 1000.0
        v = int(255 * (t ** 3))
        if v < 8:
            continue
        # Stars are very slightly blue, the way hot ones are.
        old = px[x, y]
        px[x, y] = (max(old[0], int(v * 0.88)), max(old[1], int(v * 0.92)),
                    max(old[2], v))
    return img


def glints(img, seed):
    """A few bright stars get a cross, because a lens does that."""
    d = ImageDraw.Draw(img, "RGBA")
    r = rng(seed)
    for _ in range(18):
        x = next(r) % W
        y = next(r) % int(H * 0.72)
        arm = 6 + next(r) % 16
        a = 90 + next(r) % 120
        d.line([(x - arm, y), (x + arm, y)], fill=(200, 220, 255, a))
        d.line([(x, y - arm), (x, y + arm)], fill=(200, 220, 255, a))
        d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(255, 255, 255, 255))
    return img


def world(img):
    """The ocean, as the limb of something much bigger than the frame.

    The circle is wider than the picture on purpose: at this radius the
    curve across 2560 pixels is gentle, which is what makes it read as a
    planet rather than as a hill.
    """
    radius = int(W * 1.65)
    cx = W // 2
    cy = int(H * 0.86) + radius

    world_layer = Image.new("RGB", (W, H), (0, 0, 0))
    wd = ImageDraw.Draw(world_layer)
    wd.ellipse([cx - radius, cy - radius, cx + radius, cy + radius],
               fill=SEA_DEEP)

    # Lit from up and to the left, so the water is not a flat shape.
    light = Image.new("L", (W, H), 0)
    ld = ImageDraw.Draw(light)
    for i in range(140):
        t = i / 140.0
        rr = radius - int(t * H * 0.55)
        ld.ellipse([cx - rr, cy - rr, cx + rr, cy + rr],
                   fill=int(255 * (1.0 - t) ** 1.6))
    light = light.filter(ImageFilter.GaussianBlur(40))
    world_layer = Image.composite(
        Image.new("RGB", (W, H), SEA), world_layer, light)

    mask = Image.new("L", (W, H), 0)
    md = ImageDraw.Draw(mask)
    md.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=255)
    img.paste(world_layer, (0, 0), mask)

    # The atmosphere: a bright rim just above the edge, blurred hard.
    rim = Image.new("RGB", (W, H), (0, 0, 0))
    rd = ImageDraw.Draw(rim)
    rd.ellipse([cx - radius - 7, cy - radius - 7,
                cx + radius + 7, cy + radius + 7], outline=(90, 150, 255),
               width=14)
    rim = rim.filter(ImageFilter.GaussianBlur(26))
    from PIL import ImageChops
    return ImageChops.screen(img, rim)


def haze(img):
    """A red wash high on the left, so the sky is not only blue.

    It is the mark's red at four per cent. Any more and it stops being a
    sky and starts being a gradient somebody applied.
    """
    from PIL import ImageChops
    layer = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(layer)
    for i in range(80):
        t = i / 80.0
        rr = int(W * 0.55 * (1.0 - t))
        v = int(30 * (1.0 - t))
        d.ellipse([int(W * 0.18) - rr, int(H * 0.18) - rr,
                   int(W * 0.18) + rr, int(H * 0.18) + rr],
                  fill=(int(RED[0] * v / 255), int(RED[1] * v / 255),
                        int(RED[2] * v / 255)))
    layer = layer.filter(ImageFilter.GaussianBlur(120))
    return ImageChops.screen(img, layer)


def mark(img):
    """Place the RSD wordmark over the planet's limb."""
    if not MARK.exists():
        print("no mark at %s - drawing without it" % MARK, file=sys.stderr)
        return img
    wordmark = Image.open(MARK).convert("RGBA")
    px = wordmark.load()
    for y in range(wordmark.height):
        for x in range(wordmark.width):
            r, g, b, _ = px[x, y]
            # The ground is a white that JPEG left between 250 and 255.
            if r > 244 and g > 244 and b > 244:
                px[x, y] = (r, g, b, 0)
    box = wordmark.getbbox()
    if box:
        wordmark = wordmark.crop(box)

    target_h = int(H * 0.19)
    scale = target_h / wordmark.height
    wordmark = wordmark.resize((int(wordmark.width * scale), target_h), Image.LANCZOS)

    # Over the limb on the right, where the light is - not centred,
    # because a desktop wants its middle left alone.
    fx = int(W * 0.62)
    fy = int(H * 0.60)
    img.paste(wordmark, (fx, fy), wordmark)
    return img


def main():
    img = Image.new("RGB", (W, H), (0, 0, 0))
    img = starfield(img, SEED)
    img = world(img)
    img = haze(img)
    img = glints(img, SEED ^ 0x5BF03)
    img = mark(img)

    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / "rsd-space.png"
    img.save(path)
    print("wrote %s (%dx%d)" % (path.relative_to(ROOT), W, H))


main()
