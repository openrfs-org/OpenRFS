# RSD

**Root Software Distribution** is an experimental, freestanding operating system and kernel for x86_64. It boots its own kernel, command line, and optional desktop. Linux does not run underneath it.

RSD is built to be understood, modified, and forked. Its kernel, drivers, application interface, package system, installer interface, command line, and desktop live in one source tree so a fork can make a different distribution from the same foundation.

> RSD is experimental. The installer interface is a configuration preview: it does not partition, format, or copy files to a disk. Privacy is a design goal, not a certification or a claim of anonymity or production security.

## The system

| Layer | What is here |
| --- | --- |
| Kernel | Multiboot2 x86_64 boot, memory management, interrupts, scheduling, drivers, filesystems, and native system calls |
| Command line | The default boot experience, with native commands and an authenticated path to the desktop |
| Installer | A keyboard-driven configuration flow with an explicit preview boundary |
| Desktop | An optional framebuffer session with native applications |
| SDK and packages | Native application headers, build tools, signed manifests, and package lifecycle code |

The current hardware and compatibility work is documented in [architecture](docs/ARCHITECTURE.md), [drivers](docs/DRIVERS.md), and [verification](docs/VERIFICATION.md). Passing a compatibility profile describes only that measured profile.

## Build and run

Ubuntu 24.04 is the reference build host.

```sh
sudo apt-get update
sudo apt-get install binutils gcc grub-common grub-pc-bin make mtools qemu-system-x86 xorriso
rustup target add x86_64-unknown-none
make verify
make run
```

`make verify` runs the repository's source, asset, host, and build checks. `make run` builds a bootable image and starts QEMU. See [verification](docs/VERIFICATION.md) for scenario-specific tests and the limits of their evidence.

## First boot

RSD starts at its command line:

```text
rsd$
```

Run `install` to inspect the installer configuration flow. It currently stops before disk changes. Create a local account with `useradd <name>`, then run `starty` to enter the optional desktop.

## Make your own distribution

Fork the repository, change the kernel and userspace together, and give your fork its own identity. The build, native ABI, package metadata, and tests are versioned together to make system changes reviewable. Start with [architecture](docs/ARCHITECTURE.md), [applications](docs/APPLICATION_PACKAGES.md), and the [SDK](sdk).

## Status and license

RSD is under active development. Read the feature-specific documentation before relying on hardware, storage, network, or security behavior. Source code is licensed under [GPL-3.0](LICENSE); bundled third-party assets and ports retain their own notices in the source tree.
