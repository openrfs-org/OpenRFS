<!-- SPDX-License-Identifier: GPL-3.0-only -->

# OpenRFS desktop

OpenRFS keeps the lightweight desktop as an optional graphical session. A
normal boot stops at the command line. After the first local account is
created, `starty` asks for the username and password and activates the desktop
only after successful authentication.

The desktop draws into a bounded linear surface. It does not require a hosted
widget toolkit, display server, font server, or runtime image decoder.

## Applications

- **Files** browses the desktop's bounded filesystem model.
- **Terminal** exposes the OpenRFS command line and measured userland commands.
- **Task Manager** shows a bounded process list.
- **OpenRFS Desktop Settings** changes settings implemented by this desktop.
- **OpenRFS DE Package Manager** requests package operations; signature,
  rollback, update, and repair authority remain in the kernel package service.

Native processes retain bounded xRGB content surfaces and event queues. The
desktop owns composition, focus, framing, and pointer capture.

## Appearance

The default uses the supplied OpenRFS G mark, a grey wallpaper, and a restrained
navy and blue-grey palette. Generated panel sizes are derived deterministically
from the canonical logo by `tools/make-brand-mark.py`.

## Evidence and limits

Dedicated UI scenarios still construct the desktop during boot so the QEMU
harness can inspect layout, redraw stability, input, native windows, and exact
head artifacts. The ordinary boot path deliberately skips those stages.

The desktop currently supports one measured display profile, printable ASCII,
and a small fixed application set. Its existence is not an anonymity, privacy,
security, or everyday use certification.
