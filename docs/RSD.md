<!-- SPDX-License-Identifier: GPL-3.0-only -->

# RSD desktop

RSD boots to its command line. After a local account is created, `starty` authenticates and opens the optional framebuffer desktop. The current desktop source is in `ui/desktop/`; `src/kernel/minimal_de.c` maps kernel events and framebuffer pixels to that interface.

The desktop has a root menu, launcher, multiple windows, workspaces, wallpaper, icons, and five applications: Files, Terminal, Task Manager, Settings, and Packages. Its image and icon sources are pinned in `ui/assets/sha256.json`.

The terminal viewport is connected to the kernel command line. Files, Packages, Task Manager, and Settings currently use bounded UI models. Their views do not yet query the production VFS, package service, or process registry. Their controls must be connected to those services before they represent installed-system operations.

The desktop uses a linear framebuffer and freestanding C. It does not require a display server, hosted widget toolkit, font server, or guest image decoder. The host harness in `ui/desktop/tools/` renders frames for review; the kernel links the same drawing and event code.

## Verification

`make minimal-de-host-test` exercises construction, drawing, pointer routing, window lifecycle, and terminal geometry. `make -C ui/desktop/tools all` compiles the freestanding desktop sources and its host harness. The full guest image still needs a successful kernel build and QEMU boot to verify the integrated session.
