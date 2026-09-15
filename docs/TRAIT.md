<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Trait OS desktop environment

Trait OS uses the lightweight desktop implementation imported from the
project owner's Trait-UI repository. It draws directly into a bounded linear
surface and needs no hosted widget toolkit, display server, font server, or
runtime image decoder.

## Desktop

The desktop uses the canonical Trait OS onion wallpaper, file icons, movable
overlapping windows, and a small bottom panel. The panel exposes applications,
workspaces, active windows, CPU activity, volume, network state, and a clock.
Only an event that changes state requests a redraw.

## Applications

- **Files** browses the desktop's bounded filesystem model.
- **Terminal** provides the visible window for Trait OS's kernel shell and
  measured userland commands.
- **Task Manager** shows a bounded process list and refuses to end the session.
- **Trait OS Desktop Settings** changes only settings the desktop implements.
- **Trait OS DE Package Manager** marks package changes and applies them as a
  separate step. Signed install, update, repair, and rollback authority remain
  enforced by the kernel package service described in `PACKAGE_MANAGER.md`.

The package manager and every other product label identify this as the Trait
OS desktop environment. Source comments and the provenance receipt retain the
Debian/LXDE inputs used to measure some classic desktop geometry and artwork;
that source history is not product branding.

## Native applications

Native processes keep their existing bounded xRGB surface and event-queue ABI.
The desktop owns the frame, focus, composition, and pointer capture while the
process owns only its checked content buffer.

## Evidence

The kernel runs the imported menu, file, package, settings, task-manager,
terminal, panel, and shell self-tests before desktop construction. The
installed proof then checks the live surface, repeatable redraw hash, pointer
path, keyboard focus, and application opening on the tested 1024x768 profile.
QEMU captures must come from the exact commit being reviewed.

## Current limits

The desktop currently supports one measured display profile, printable ASCII,
and a small fixed application set. Its redraw adapter converts a full frame
after changed events. This is development software and the desktop proof is
not an anonymity, privacy, or everyday-use certification.
