<!-- SPDX-License-Identifier: GPL-3.0-only -->

# OpenRFS desktop

OpenRFS keeps the lightweight desktop as an optional graphical session. A
normal boot stops at the command line. After the first local account is
created, `starty` asks for the username and password and activates WVRM
only after successful authentication. WVRM is the terminal on the woven root
background, with its pixel Files app opened from the root right-click menu.
The v2.4.0 release image predates this integration.

The desktop draws into a bounded linear surface. It does not require a hosted
widget toolkit, display server, font server, or runtime image decoder.

## Session and applications

The session opens a terminal on the bare X-root-style weave. Its fvwm-style
frame, bitmap text, root menu, and optional iconified windows come from the
imported desktop. The terminal viewport is connected to the real OpenRFS
kernel console: typing `gfetch`, `network`, or another supported shell command
uses the same command implementation as the boot CLI. The root menu opens by
clicking bare desktop space.

WVRM Files browses read-only snapshots of mounted System and Data directories
through the production VFS after sign-in. It refuses directories that exceed
its bounded view capacity; it never applies its imported in-memory edit
operations to installed files. The imported Packages, Task Manager, and
Settings windows remain in-memory models and are not exposed as installed
system operations. The supported session exposes the real terminal, Files,
the root menu and launcher, and the self-contained gears window.

Native processes retain bounded xRGB content surfaces and event queues. The
desktop owns composition, focus, framing, and pointer capture.

## Appearance

WVRM has no panel, dock, tray, or wallpaper. It uses the X root
weave, sixteen-colour palette, fvwm-style frames, Misc-Fixed bitmap fonts, and
the owner-supplied fish mark printed by `gfetch`. The older desktop assets
remain in the repository for historical boot proofs.

## Evidence and limits

Dedicated historical UI scenarios still construct the earlier desktop during
boot to inspect its layout, redraw stability, input, and native windows. The
ordinary boot path skips those stages; the authenticated `starty` path now
constructs WVRM. A separate host test and QEMU login capture
exercise that production path, including the real `gfetch` command and
full-frame pixel comparisons.

The desktop currently supports one measured display profile, printable ASCII,
and a small fixed application set. Its existence is not an anonymity, privacy,
security, or everyday use certification.
