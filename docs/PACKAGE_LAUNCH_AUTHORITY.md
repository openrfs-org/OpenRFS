<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native package launch authority

This is a design and gap record for the unfinished draft. It does not claim
that installed native programs are authenticated at launch.

## Production path and present trust boundary

`package_control` verifies a signed repository and package at install, then
`package_builder` writes a generation database and owned files through
`package_service`. The current shell and desktop launch System manifests;
the only current caller of `native_process_launch_installed` is the
`native-openrfs` QEMU scenario. Before the bounded mitigation below, this
installed-program entry point checked only that the manifest pathname had the
`pkgstate/gen/<hex>/<hex>/root/` shape. `load_process` reads that
manifest and its sibling ELF from writable Data. Rust
`openrfs_native_image_validate` parses the requested capability mask and checks
that the ELF SHA-256 matches the hash **inside the same mutable manifest**.
`process->manifest.capabilities` then gates syscalls. This checks internal
consistency and ELF format, not publisher authorization.

The attacker-controlled bytes are the Data volume's manifest, ELF, generation
database, authority record, repository floor and upload cache. Persistence is
on FAT32 or ext4 Data. The database and authority record use unkeyed hashes;
their digest can be recomputed after a writable-volume edit. Before the
mitigation, a stale but well-formed generation path was not compared with the
active authority. A modified supported capability bit at manifest offset 28
can therefore remain parseable without changing its executable hash. The
manifest parser's existing positive capability tests demonstrate this format
property. A read-only extraction from the successful native-porting artifact
for PR-head tree `980ba3d7fd2abaa63e76ede075b2b4a4fdca765a` showed
generation 3 `bin/CHESS.MAN` with mask `0xDD` and SHA-256
`B5E4CF200DB41587B915855EB99B6B2314F07F2303C8A8465D4327ECCEF3B025`.
The signed `org.libsdl.chess` 2.0.0 package authenticates that file hash and
those requested capabilities. A scratch host probe compiled the production
Rust `native_image::validate` against the extracted manifest and ELF. It
admitted both the original pair and the same ELF with the writable mask changed
to `0x1DD` (adding network), although the manifest hash changed. This proves
the image validator alone cannot supply package authority. This scratch probe
is not an end-to-end installed launch test.

## Bounded launch mitigation

The installed-program entry point now invokes `package_service_recover`
before reading a Data manifest. Recovery selects one generation and checks
its complete owned file tree against the generation database. The entry point
compares that selected generation with the exact 16-digit generation in the
manifest path, rejects noncanonical suffix components, and refuses a recovery
error or mismatch. The parser self-test and `native-openrfs` guest scenario
reject traversal components. The `native-openrfs`
guest scenario changes the installed manifest's network capability byte,
requires launch refusal, restores the original byte, and requires recovery
to validate the generation again. The unmodified installed program launches
once before this control. This prevents a single-file capability edit and an
inactive-generation path under an unchanged database. It is not
publisher-authenticated admission:
an attacker who can rewrite both the database and authority on writable Data
can forge their unkeyed digests, and a whole-volume rollback remains possible.

The immutable publisher-key table is compiled into the kernel. It is only an
external trust root after the exact kernel and configuration are authorized by
an enforcing boot chain. The current detached host verifier does not provide
that boot enforcement. A Data-only rollback floor cannot provide freshness
against whole-volume rollback.

## Required enforcing transaction

1. Preserve the complete signed package envelope, or introduce a new signed
   install statement that covers the exact installed manifest, every executable
   and library digest, package identity, requested capability mask, generation
   policy and publisher key identity. The existing package signature cannot be
   verified from a bare digest after discarding its signed bytes.
2. Durably bind that evidence to the generation selected by package recovery.
   Every launch must resolve the selected generation, reject other generation
   paths, validate the signature against a non-revoked built-in publisher key,
   check the installed manifest and ELF against the signed content, and derive
   runtime grants only as a subset of the signed request and kernel policy.
3. Keep the source bytes or signed statement owned across `spawn`, ELF loading,
   dynamic library resolution, and grant installation. Recheck after any
   filesystem operation that could replace an inode; a path-only check is
   vulnerable to time-of-check/time-of-use replacement.
4. Define an operator-controlled external generation floor before claiming
   whole-volume rollback refusal. Without one, report that an old signed
   package and matching old volume can be replayed.

The negative gate must reject altered manifest masks, names, binaries,
installed records, stale generations, database rollback, revoked keys and
wrong keys. It must launch an unmodified signed package and exercise its
granted and denied syscalls. No part of this gate may turn a hash stored on the
same writable Data volume into a claimed signature.
