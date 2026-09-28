# RSD

The three faces of RSD, in one tree: the installer, the command
line, and the desktop.

    desktop/    the graphical session — framebuffer, no display server
    console/    the command line and the installer
    assets/     fonts, icons, the mark, the wallpaper
    docs/       design references
    tools/      the harnesses and the generators

Both halves are freestanding C. `desktop/` draws to a framebuffer and
`console/` to a grid of cells; neither knows what a pixel is until the
harness turns one into a PNG. That split is why the same code can be
screenshotted here and run on the metal there.

## What it is being made to look like

2010, and specifically: KDE 4's Oxygen for the desktop, Nautilus for the
file manager, Konsole for the terminal, `bsdinstall` for the installer,
and a BSD boot for the console.

Every number taken from those is written down in
[`docs/REFERENCES.md`](docs/REFERENCES.md) with the file it was read
from. Every pixel of artwork that is not ours is in
`assets/icons/SOURCE.txt` with its licence and the person who drew it.

## Build

    make -C console/tools all    # command line and installer harnesses
    make -C desktop/tools all    # desktop harness

Run `make -C console/tools check` for the console and installer checks.
The built programs are `console/build/rsd`, `console/build/rsdinstall`,
and `desktop/build/rsd`.

## The icons

    sudo apt-get install oxygen-icon-theme tango-icon-theme
    python3 tools/vendor-icons.py

Oxygen is KDE's, LGPL-3+. Tango is freedesktop.org's, public domain.
Neither is redrawn and neither is traced — the files are copied byte for
byte and their hashes recorded.
