# OpenRFS identity

OpenRFS is the product, kernel, command line, desktop, SDK, and release name.
Use that spelling in public text. Code identifiers and paths use openrfs or
OPENRFS when spaces or mixed case are unsuitable. The shell prompt is
openrfs$.

The public description is:

> OpenRFS is a small Unix-like operating system built from scratch, with
> privacy as its design goal.

That sentence states the direction of the project. It is not a claim that the
current build is anonymous, certified, or suitable for high-risk everyday use.

## Logo

The canonical mark is assets/openrfs/logo.png, a transparent runtime derivative
of the project-owner-supplied fish artwork. The original JPEG is preserved as
assets/openrfs/logo-source.jpeg, with a lossless decoded PNG beside it. Keep
the red body, black outline, white eyes, pink tongue, proportions, and clear
space. Do not add text, a slogan, glow, shadow, texture, or another symbol.

## Wallpaper

assets/openrfs/wallpaper.png preserves the project-owner-supplied sketch
artwork in a 4:3 white frame without cropping the drawing. The original JPEG is
preserved as assets/openrfs/wallpaper-source.jpeg. The build converts the PNG
to the bounded SPW3 format; the guest does not parse PNG or run an image
library.

The adjacent source receipts record the original filenames, source hashes,
derivative hashes, dimensions, and deterministic transformations.
tools/verify-ui-assets.py pins those files and the generated runtime assets.

## Product copy

Prefer direct descriptions of implemented behavior. Reserve PASS, READY, and
ONLINE for measured diagnostics. Every privacy or security statement must name
the boundary and evidence that support it.
