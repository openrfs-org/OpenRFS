# RSD interfaces

The current interface tree is `ui/`. Its console, installer, desktop, applications, icons, mark, wallpaper, fonts, generators, and host harnesses are based on RSD UI commit `302d19068d1cfc22ca10ad2131092cde3b301e0e`.

## Runtime wiring

- `src/kernel/installer_ui.c` maps kernel keyboard events and framebuffer cells to `ui/console/src/{term,ui,install}.c`.
- `src/kernel/minimal_de.c` maps kernel pointer and keyboard events to `ui/desktop/src/` and draws the desktop into the guest framebuffer.
- The kernel shell retains command dispatch, storage, account, process, and network operations. Its visible identity is RSD. The console UI model in `ui/console/` also contains the target command line design, but command routing and boot presentation still require integration.

The installer is a configuration preview. Its write and verification progress are in-memory UI states. It does not partition, format, or copy a disk. The boundary stays visible to users until a transactional backend exists.

## Build checks

```sh
make installer-port-test minimal-de-host-test
make -C ui/console/tools check
make -C ui/desktop/tools all
```

The interface code is freestanding C. The harnesses are host-only programs. Assets retain their source and license notices under `ui/assets/`.
