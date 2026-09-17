<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Third-party sources and visual assets

OpenRFS's build is offline and deterministic. The exact third-party source
files used by the runtime and UI are committed and licensed beside the code or
assets. Runtime sources are pinned to exact upstream Git objects or release
archives. Visual sources are converted by host tools and then parsed through
bounded Rust formats before C draws them.

## OpenRFS identity artwork

The project owner supplied the current fish logo and sketch wallpaper directly.
The original JPEG attachments are preserved as
assets/openrfs/logo-source.jpeg and assets/openrfs/wallpaper-source.jpeg; the
decoded runtime PNGs are derived assets. Their source hashes, derivative
hashes, dimensions, and deterministic transformations are recorded in the
adjacent receipts. No upstream author or license is inferred beyond the
provenance the user provided.

## ext4plus

The sole ext4 implementation candidate is the official
[`arihant2math/ext4plus`](https://github.com/arihant2math/ext4plus) repository at
commit `ec7e8443e474376977bb752cde370762226a5a50`, Git tree
`a4aea888632546b2bbfbefa97b43ca6c8f945fc8`. The exact `no_std` source tree,
README, MIT license, and Apache-2.0 license are retained under
`vendor/ext4plus/`. OpenRFS selects the MIT terms for GPL-3.0-only distribution;
both upstream notices remain available.

The local manifest removes workspace inheritance and development-only inputs;
the local lock resolves that manifest to the repository's exact offline Cargo
mirror, matching the kernel lock. The implementation source is otherwise
pinned to the recorded tree apart from reviewable OpenRFS deltas. The accepted
runtime profile is writable only through OpenRFS's retained mutation stage and
ordered JBD2/NVMe executor. Upstream does not implement journaled writes by
itself; the local delta supplies the checksummed transaction, recovery,
checkpoint, revocation, retry, and power-cut-tested durability boundary.
`vendor/ext4plus/OPENRFS-PORT.md` records that boundary and the exact feature
configuration.

## BearSSL

TLS uses BearSSL 0.6 from the official
[`bearssl.org` Git repository](https://www.bearssl.org/gitweb/?p=BearSSL;a=summary),
annotated tag object `7d8e767e79bb1750345e571ec89cca1da13b52df`,
commit `8ef7680081c61b486622f2d983c0d3d21e83caad`, and Git tree
`3d0709034c2b5eb735d43ff639411ac31e76153b`. The retained `inc/` and `src/`
trees are byte-for-byte upstream files. BearSSL's MIT license and README are
preserved under `vendor/bearssl/`.

OpenRFS disables BearSSL's hosted entropy and time adapters and its optional
SSE2, AES-NI, and POWER8 implementations. The SDK wrapper supplies native
entropy, validated realtime, trust anchors, a canonical DNS hostname,
monotonic transport deadlines, and a bounded TLS 1.2 cipher profile. The
cryptographic primitives and protocol state machine are unmodified.

## zlib

The compression source is the official zlib 1.3.2 release archive from
[`zlib.net`](https://zlib.net/), SHA-256
`bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`.
It corresponds to annotated tag object
`216c70c020aa53f0c40920d155f808b6b59c9acb` and commit
`da607da739fa6047df13e66a2af6b8bec7c2a498` in the official repository.
The zlib license is retained verbatim.

OpenRFS carries the byte-exact public headers and nine-source `Z_SOLO` core for
bounded in-memory deflate/inflate and checksums. Hosted gzip-file adapters and
the allocation-backed `compress*` convenience API are excluded.
`vendor/zlib/SOURCE-MANIFEST.sha256` pins each retained upstream file, while
`vendor/zlib/OPENRFS-PORT.md` defines the freestanding build and allocator
contract. The reproducible SDK installs this profile as the static `libz.a`.
OpenRFS's bounded native shared-object loader is separate; zlib itself is not
shipped as an OpenRFS DSO. Authenticated OpenRFS DSOs can share immutable RX
physical pages across processes, but the current zlib SDK artifact remains the
reproducible static `libz.a` described above.

## SDL 2

The multimedia compatibility layer is the official SDL 2.32.10 source from
[`libsdl-org/SDL`](https://github.com/libsdl-org/SDL) at commit
`5d249570393f7a37e037abf22cd6012a4cc56a71` (tag `release-2.32.10`). The
retained `include/` and `src/` trees and upstream zlib license are committed
under `vendor/sdl2/`; exact retained-tree and license hashes are recorded in
`vendor/sdl2/UPSTREAM-COMMIT.txt`.

The byte-exact upstream `test/testdrawchessboard.c` application used by the
signed-install proof is retained under `apps/upstream-sdl-chess/`. Its source
URL, Git blob, SHA-256, license, and bounded OpenRFS harness are recorded in the
adjacent `UPSTREAM.md`.

OpenRFS adds an explicitly selected `__OPENRFS__` configuration and native
OpenRFS video/input, PCM audio, pthread/futex, timer, and preference-filesystem
backends. Disabled subsystems and the evidence boundary are recorded in
`vendor/sdl2/OPENRFS-PORT.md` and `docs/SDL.md`. No Linux compatibility layer or
SDL dummy video/audio backend is used.

## Inter

The installed UI-font compatibility boundary uses `InterVariable.ttf` from the official Inter repository
at commit `353b61b9f4430d5f420d56605a6e7993e0941470`. Inter is distributed under
the SIL Open Font License 1.1; the exact license is committed as
`assets/fonts/Inter-LICENSE.txt`.

`tools/rasterize-inter-ui.py` converts printable ASCII at the pinned size into
the committed alpha atlas and proportional metrics. The ordinary build does
not need Pillow and does not parse TrueType: `tools/make-ui-font-asset.py`
packs those committed intermediates into SUF2, whose exact metrics, length,
fingerprint, glyph ranges, and alpha data are validated before installation.

## OpenRFS UI desktop assets

The active desktop code and its older generated icon and font tables began at
the project owner's historical UI source commit
`eb6d32f8bee63025508af992656a7815fa3285c1`, tree
`93319863b82f22b23d8415f04b733cebed6d29f1`. The code is GPL-3.0-only.

`docs/OPENRFS_UI_PROVENANCE.md` records the integration changes and exact
generated-header hashes. Debian/LXDE-derived geometry and artwork remain
attributed because the origin and license of imported data do not change when
the operating system is renamed.
