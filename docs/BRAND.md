# OpenGAT identity

`OpenGAT` is the product, kernel, command line, desktop, SDK, and release name.
Use that spelling in public text. Code identifiers and paths use `opengat` or
`OPENGAT` when spaces or mixed case are unsuitable. The shell prompt is
`opengat$`.

The public description is:

> OpenGAT is a small Unix-like operating system built from scratch, with
> privacy as its design goal.

That sentence states the direction of the project. It is not a claim that the
current build is anonymous, certified, or suitable for high-risk everyday use.

## Logo

The canonical mark is [`assets/opengat/logo.png`](../assets/opengat/logo.png), a
transparent runtime derivative of the user-supplied source stored as
[`assets/opengat/logo-source.png`](../assets/opengat/logo-source.png). The mark
must keep its navy ring, blue-grey wedge, proportions, and clear space. Do not
add a slogan, glow, shadow, outline, texture, or another symbol.

## Wallpaper

[`assets/opengat/wallpaper.png`](../assets/opengat/wallpaper.png) is the default
wallpaper: the OpenGAT mark centered on a restrained grey field. The build
converts it to the bounded SPW3 format; the guest does not parse PNG or run an
image library.

The adjacent source receipts record the input hashes, derivative hashes, tool,
and transformation request. `tools/verify-ui-assets.py` pins those files and
the generated runtime assets.

## Product copy

Prefer direct descriptions of implemented behavior. Reserve `PASS`, `READY`,
and `ONLINE` for measured diagnostics. Every privacy or security statement must
name the boundary and evidence that support it.
