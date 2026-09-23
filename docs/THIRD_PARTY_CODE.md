<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Third-party code inventory

OpenRFS's kernel build is self-contained and makes no registry or network access.
`src/rust/Cargo.lock`, every vendored `.cargo-checksum.json`, and
`.cargo/config.toml` jointly pin and authenticate the complete Rust source
closure. License texts distributed by each package remain in its vendor
directory.

## ext4 implementation

`ext4plus` 0.1.0-rc.2 is pinned to upstream commit
`ec7e8443e474376977bb752cde370762226a5a50` and is available under
MIT OR Apache-2.0. `vendor/ext4plus/UPSTREAM-COMMIT.txt` records the repository,
commit date, Git tree, and license; `vendor/ext4plus/OPENRFS-PORT.md` records the
OpenRFS delta and the read-write safety gate.

## Locked runtime closure

| Package | Version | License |
| --- | ---: | --- |
| async-lock | 3.4.2 | Apache-2.0 OR MIT |
| async-openrfs | 0.1.92 | MIT OR Apache-2.0 |
| bitflags | 2.13.1 | MIT OR Apache-2.0 |
| crc | 3.4.0 | MIT OR Apache-2.0 |
| crc-catalog | 2.5.0 | MIT OR Apache-2.0 |
| event-listener | 5.4.2 | Apache-2.0 OR MIT |
| event-listener-strategy | 0.5.4 | Apache-2.0 OR MIT |
| lock_api | 0.4.14 | MIT OR Apache-2.0 |
| maybe-async | 0.2.11 | MIT |
| pin-project-lite | 0.2.17 | Apache-2.0 OR MIT |
| proc-macro2 | 1.0.107 | MIT OR Apache-2.0 |
| quote | 1.0.47 | MIT OR Apache-2.0 |
| scopeguard | 1.2.0 | MIT OR Apache-2.0 |
| spin | 0.10.1 | MIT |
| syn | 2.0.119 and 3.0.4 | MIT OR Apache-2.0 |
| unicode-ident | 1.0.24 | (MIT OR Apache-2.0) AND Unicode-3.0 |

The async crates appear because they are manifest-level dependencies of the
single upstream implementation. OpenRFS disables ext4plus's async and
multi-threaded paths; Cargo still resolves the published dependency closure.
No upstream test images, hosted adapters, or task runner are included.

## TLS implementation

BearSSL 0.6 is pinned to upstream commit
`8ef7680081c61b486622f2d983c0d3d21e83caad` and is available under the MIT
license. The SDK builds the unmodified `inc` and `src` trees into a separate
deterministic `libbearssl.a`; `vendor/bearssl/UPSTREAM-COMMIT.txt` records the
official source, tag, commit date, and Git tree.

OpenRFS disables BearSSL's host `/dev/urandom`, Win32 random, host-time, SSE2,
AES-NI, and POWER8 compile-time paths. An OpenRFS client must inject kernel-backed
entropy and explicitly set the validated realtime clock. Merely linking the
archive is not a TLS or HTTPS success claim: hostname, chain, time, transport,
shutdown, and negative-path evidence belong to the client integration and QEMU
tests.

## Guest signature implementation

Monocypher 4.0.3 is pinned to upstream tag/commit
`ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f` and is available under
BSD-2-Clause OR CC0-1.0. The retained core and optional Ed25519/SHA-512 source
files are byte-for-byte upstream; `vendor/monocypher/UPSTREAM-COMMIT.txt` and
`SOURCE-MANIFEST.sha256` record exact provenance.

OpenRFS compiles those sources freestanding with MMX/SSE disabled. The package
trust wrapper rejects non-canonical encodings, pure low-order points,
negative-zero encodings, and `S >= L` before or during the upstream cofactored
equation check, and supports the package format's zeroed embedded-signature
range without copying a 256 MiB object. The exact profile and non-uniqueness
caveat are recorded in `vendor/monocypher/OPENRFS-PORT.md`.

## Upstream hardware drivers

Three projects' drivers are vendored byte-for-byte and run through
compatibility layers; `docs/UPSTREAM_DRIVERS.md` lists every driver with its
binding rule and QEMU evidence, and `make driver-provenance` checks each
project's `SOURCE-MANIFEST.sha256`.

- **iPXE** is pinned to commit `744cdb451ef28bc894df72b6b40fdf1fda04acfc`
  (2026-09-22). Every vendored file declares GPL-2.0-or-later,
  GPL-2.0-or-later OR UBDL-1.0, GPL-1.0-or-later or BSD-2-Clause; GPL-2.0-only
  files are excluded. Its network drivers compile against
  `ports/ipxe/include`; its USB stack (USB core, xHCI, EHCI, UHCI, hub,
  CDC-ECM, RNDIS, scheduler) is partially linked with its linker tables kept
  in order and every symbol but the glue entry points made local.
- **SeaBIOS** is pinned to commit `81ec9ec0bcf45df11fb7f98339ec9036b546fca0`
  (2026-07-31) under LGPL-3.0. Its storage, USB and TPM drivers run in a
  32-bit flat-mode environment recreated by `ports/seabios`; its VGA drivers
  (`vgasrc`) are built once per card type by `ports/seavga`. Two files are
  compiled through wrappers that correct an LP64 structure layout without
  changing the vendored source (`ports/seabios/lp64`, `ports/seavga/lp64`).
- **MINIX 3** is pinned to commit `4db99f4012570a577414fe2a43697b2f239b699e`
  (2018-11-14, the repository's last) under the MINIX 3 BSD-3-Clause
  licence. Its ES1370 and Sound Blaster 16 drivers run with
  `ports/minix/audio_glue.c` in libaudiodriver's place.

Each `vendor/<project>/UPSTREAM-COMMIT.txt` records the repository, commit
date, Git tree and licence analysis, and each `OPENRFS-PORT.md` records what
the compatibility layer provides and where it differs from the upstream
runtime.

## Host signature implementation

Format-v3 package construction and inspection use the distribution-provided
Python `cryptography` Ed25519 implementation. It is a host build dependency,
not code linked into or distributed with OpenRFS. The Linux verification image
installs `python3-cryptography`, requires a real sign/verify round trip, and
also tests fail-closed behavior with Python site packages disabled.
