OpenGAT
-------

OpenGAT is a small Unix-like operating system built from scratch for x86_64.
It boots into its own command line; Linux does not run underneath the guest.
The kernel, drivers, shell, application ABI, package system, and optional
lightweight desktop are maintained in this repository.

Privacy is a design goal. OpenGAT is development software and is not an
anonymity, privacy, security, or everyday-use certification.

Quick Start
-----------

Ubuntu 24.04 is the reference build host.

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

First Boot
----------

OpenGAT starts at a bare `opengat$` prompt. Create the local account:

```text
opengat$ useradd alice
New password (8-64 characters):
Confirm password:
OpenGAT user created. Run 'starty' to enter the desktop.
```

Passwords are not echoed. On later boots, start the optional desktop with:

```text
opengat$ starty
Username: alice
Password:
```

The account record is stored on the writable data volume. OpenGAT refuses to
create it when the initialized random source or durable storage is unavailable.
The current account model supports one local user.

Current State
-------------

OpenGAT currently includes:

* A freestanding x86_64 kernel written in C, Rust, and assembly.
* A command line, a lightweight desktop, and bounded native applications.
* NVMe, USB, audio, IPv4 networking, FAT32, and an ext4/JBD2 integration
  boundary.
* Signed package manifests, explicit trust roots, TLS, and checked native
  process interfaces.
* QEMU scenarios for normal boot, deliberate faults, reboot persistence, and
  selected power-cut recovery cases.
* Measured compatibility profiles for selected BusyBox programs and native
  ports of Lua, SQLite, SDL 2, zlib, and BearSSL.

The measured profiles are small. OpenGAT does not currently provide a complete
POSIX environment, multi-user permissions, full process control, IPv6,
firewalling, Wi-Fi, an IOMMU, or a completed privacy threat model.

Documentation
-------------

* Architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
* Command-line accounts and `starty`: [docs/LOGIN.md](docs/LOGIN.md)
* Desktop: [docs/OPENGAT.md](docs/OPENGAT.md)
* Application loader: [docs/APPLICATION_LOADER.md](docs/APPLICATION_LOADER.md)
* Package manager: [docs/PACKAGE_MANAGER.md](docs/PACKAGE_MANAGER.md)
* TLS: [docs/TLS.md](docs/TLS.md)
* Persistent FAT32: [docs/FAT32.md](docs/FAT32.md)
* ext4/JBD2 boundary: [docs/EXT4.md](docs/EXT4.md)
* Verification: [docs/VERIFICATION.md](docs/VERIFICATION.md)
* Third-party provenance: [docs/THIRD_PARTY_ASSETS.md](docs/THIRD_PARTY_ASSETS.md)

Contributing
------------

OpenGAT accepts focused changes that can be reviewed and reproduced.

* Read [CONTRIBUTING.md](CONTRIBUTING.md) before starting work.
* Branch from the latest `origin/main` and keep unrelated changes out of the
  pull request.
* Install the repository hooks with `make hooks`.
* Run `make lint` for documentation-only changes. Run `make verify` and
  `make smoke` for code changes, and `make qemu-tests` for low-level kernel,
  driver, filesystem, or ABI work.
* Include the exact commands, results, and artifacts needed to review the
  change.
* Sign commits and use your own name and email. Do not add bot or tool
  co-authors.

The full workflow, evidence matrix, review boundaries, and authorship rules are
in [CONTRIBUTING.md](CONTRIBUTING.md).

License
-------

OpenGAT is licensed under [GPL-3.0-only](LICENSE).
