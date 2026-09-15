<p align="center">
  <img src="assets/opengat/logo.png" alt="OpenGAT logo" width="180">
</p>

<h1 align="center">OpenGAT</h1>

<p align="center"><strong>A small Unix-like operating system built from scratch, with privacy as its design goal.</strong></p>

<p align="center">
  <a href="https://github.com/saudaljuaid/OpenGAT/actions/workflows/verify.yml"><img src="https://github.com/saudaljuaid/OpenGAT/actions/workflows/verify.yml/badge.svg" alt="verification status"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--only-595976" alt="GPL-3.0-only"></a>
</p>

OpenGAT boots to its own command line. The desktop is optional: create the
first local user, run `starty`, and authenticate before the graphical session
is constructed. The kernel, drivers, shell, application ABI, package system,
and desktop are part of this repository; Linux is not underneath the guest.

<p align="center">
  <img src="assets/opengat/wallpaper.png" alt="OpenGAT grey wallpaper" width="768">
</p>

## First boot

At the bare `opengat$` boot prompt, create the single local account:

```text
opengat$ useradd alice
New password (8-64 characters):
Confirm password:
OpenGAT user created. Run 'starty' to enter the desktop.
```

Passwords are not echoed. On later boots, start the desktop with:

```text
opengat$ starty
Username: alice
Password:
```

The account record is stored on the writable data volume. OpenGAT refuses to
create it when the initialized random source or durable storage is unavailable.
The current account model supports one local user.

## What exists today

- A freestanding x86_64 kernel written in C, Rust, and assembly.
- A command line, a lightweight desktop, and bounded native applications.
- NVMe, USB, audio, IPv4 networking, FAT32, and an ext4/JBD2 integration
  boundary.
- Signed package manifests, explicit trust roots, TLS, and checked native
  process interfaces.
- QEMU scenarios for normal boot, deliberate faults, reboot persistence, and
  selected power-cut recovery cases.
- Measured compatibility profiles for selected BusyBox programs and native
  ports of Lua, SQLite, SDL 2, zlib, and BearSSL.

The measured profiles are small. OpenGAT does not currently provide a complete
POSIX environment, multi-user permissions, full process control, IPv6,
firewalling, Wi-Fi, an IOMMU, or a completed privacy threat model. This is
development software and is not an anonymity, privacy, security, or everyday
use certification.

## Build and boot

Ubuntu 24.04 is the reference host:

```sh
sudo apt-get install binutils gcc grub-common grub-pc-bin make mtools \
    qemu-system-x86 xorriso
rustup target add x86_64-unknown-none

make verify
make run
```

`make verify` runs the local acceptance suite. Hardware, filesystem, process,
networking, and ABI sweeps run in GitHub Actions and must be inspected at the
exact commit they tested.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Command-line accounts and `starty`](docs/LOGIN.md)
- [Desktop](docs/OPENGAT.md)
- [Brand and artwork](docs/BRAND.md)
- [Application loader](docs/APPLICATION_LOADER.md)
- [Package manager](docs/PACKAGE_MANAGER.md)
- [TLS](docs/TLS.md)
- [Persistent FAT32](docs/FAT32.md)
- [ext4/JBD2 boundary](docs/EXT4.md)
- [Verification](docs/VERIFICATION.md)
- [Third-party provenance](docs/THIRD_PARTY_ASSETS.md)

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.
OpenGAT is licensed under [GPL-3.0-only](LICENSE).
