<!-- SPDX-License-Identifier: GPL-3.0-only -->

# OpenRFS desktop

OpenRFS keeps the lightweight desktop as an optional graphical session. A
normal boot stops at the command line. After the first local account is
created, `starty` asks for the username and password and activates the minimal
desktop only after successful authentication. This is the `Trait-UI` desktop
referenced by the `opengatcommandline` console project, not the earlier
Debian-styled desktop used by historical boot proofs.

The desktop draws into a bounded linear surface. It does not require a hosted
widget toolkit, display server, font server, or runtime image decoder.

## Session and applications

The session opens a terminal on the bare X-root-style weave. Its fvwm-style
frame, bitmap text, root menu, and optional iconified windows come from the
imported desktop. The terminal viewport is connected to the real OpenRFS
kernel console: typing `gfetch`, `network`, or another supported shell command
uses the same command implementation as the boot CLI. The root menu opens by
clicking bare desktop space.

The imported source also contains Files, Packages, Task Manager, and Settings
windows backed by bounded in-memory UI models. They are not yet views of the
production VFS, package service, or process registry, so this integration
does not expose them in the supported root menu or launcher. Their controls
must be wired to owned kernel services before they can be enabled as
installed-system operations. The supported session exposes the real terminal,
the root menu and launcher, and the self-contained gears window.

Native processes retain bounded xRGB content surfaces and event queues. The
desktop owns composition, focus, framing, and pointer capture.

## Appearance

The minimal session has no panel, dock, tray, or wallpaper. It uses the X root
weave, sixteen-colour palette, fvwm-style frames, Misc-Fixed bitmap fonts, and
the owner-supplied fish mark printed by `gfetch`. The older desktop assets
remain in the repository for historical boot proofs.

## Evidence and limits

Dedicated historical UI scenarios still construct the earlier desktop during
boot to inspect its layout, redraw stability, input, and native windows. The
ordinary boot path skips those stages; the authenticated `starty` path now
constructs the minimal desktop. A separate host test and QEMU login capture
exercise that production path, including the real `gfetch` command and
full-frame pixel comparisons.

The desktop currently supports one measured display profile, printable ASCII,
and a small fixed application set. Its existence is not an anonymity, privacy,
security, or everyday use certification.
