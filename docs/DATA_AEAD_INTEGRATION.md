<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Data AEAD integration boundary (design, not enabled)

## Production path and authority

`openrfsfs_open_options`, `read`, `pread`, `write`, `seek`, `fstat`,
`ftruncate`, `truncate`, `rename`, `rename_replace`, `stat_path`, directory
iteration, and `sync` enter `src/kernel/vfs.c`. VFS delegates regular files to
the FAT32 or ext4 backend. Shell file commands, native syscalls, package
recovery, uploads, and the installed-package launcher share this path. The
ordinary boot now enables a monotonic VFS Data login lock before the shell
starts. It admits only the exact credential record paths and their parent
metadata until `account_authenticate` succeeds. Directory enumeration is
refused, and existing file descriptions are checked again for reads and
writes. The normal QEMU test still runs its legacy package recovery before
enabling the same lock; ordinary boot does not run that recovery. This is
pre-login access control, not at-rest encryption or migration. A change
confined to a shell command or an unused crypto module would leave production
writes in plaintext.

The v2 account record wraps a random 32-byte Data key. Only successful
`account_authenticate` makes it available in RAM. The record is on the Data
volume in plaintext, so credential paths need a narrow raw-file exception
before login. No key stored on that medium can establish freshness against
an offline attacker. Boot verification, operator enrollment, and an external
rollback floor are separate prerequisites for a complete claim.

## Attacker input, ownership, and failure

Treat every file header, ciphertext chunk, FAT/ext4 directory entry, length,
generation, and persisted transaction record as attacker controlled. Paths,
flags, offsets, mode, and file bytes also arrive from native applications.
VFS owns public descriptions and mount/vnode pins; the backend owns raw file
handles. A transparent layer must avoid recursing through public VFS calls or
holding metadata locks over storage and cryptography. It must give open file
descriptions a defined behavior across rename, replacement, and unlink.

The bounded envelope in `data_aead.c` authenticates a canonical path and
declared length in its header, then XChaCha20-Poly1305 protects each fixed
4 KiB plaintext chunk under a file-ID-separated key. A new content revision
needs a new random file ID and nonce for every chunk. This prevents taking a
chunk from a prior revision and inserting it into a new one. A complete old
file or old disk snapshot can still be rolled back. Names, directory layout,
physical sizes, freed plaintext clusters, and SSD remanence remain visible.

`data_aead_rewrite.c` can stream an authenticated old file into a distinct
shadow with a partial write, sparse extension or truncate. It verifies every
old chunk, including truncated-away chunks, allocates a new random file ID
and nonces, and wipes its caller-owned bounded workspace. Host fault tests
exercise tampering, wrong keys, entropy refusal, and disk-full writes. It
does not publish or recover the shadow, preserve filesystem metadata, migrate
plaintext, or intercept the production VFS. Those remain required before
the Data namespace is encrypted.

**Do not wire in-place encrypted writes.** A torn header or chunk can make a
valid old file unreadable. For partial and sparse writes, truncate, metadata
changes, and rename, build a bounded shadow file, seal and verify it, sync it,
then publish it. The ext4 path can use its atomic replacement callback; FAT32
needs an authenticated transaction record and source backup because it cannot
replace an existing open file atomically. Recover the old or new complete
version on reopen. Disk full before publication must leave the old file
readable. A torn transaction must never fall back to interpreting ciphertext
or a newly planted file as plaintext.

Migration must enumerate the whole legacy Data tree after login, stage an
encrypted replacement per file, verify it, publish it durably, then retire
the plaintext entry. A cut at each transition must permit deterministic
resume. A durable authenticated completion state must close the legacy
plaintext acceptance window. Even after a successful migration, old clusters
and storage snapshots can retain plaintext; a filesystem cannot promise
secure erase. Account deletion must refuse while any data remains unencrypted
or a transaction is pending. Once encrypted, deletion can destroy the wraps,
but physical rollback of old wraps remains possible without external state.
The current `userdel` allows removal only after a bounded scan finds no
noncredential files. It does not quiesce concurrent native writes, erase
freed plaintext, or authorize deletion of an encrypted but nonempty Data tree.

## Required proof before activation

Host and QEMU cases must cover empty and maximum files, partial-block writes,
sparse gaps, append, read and `pread`, concurrent descriptions, rename and
replacement, directory rename, metadata and symlinks, truncate, fsync and
reopen, changed header/chunk/path, wrong key, swapped and rolled-back
revisions, all disk-full and power-cut transitions, interrupted migration,
and raw-media inspection. Package recovery and native launch need an explicit
post-login ordering. The full declared QEMU suite and affected integration
scenarios must pass on the exact implementation tree. No claim of Data
confidentiality is made by this design document or the envelope primitive
alone.
