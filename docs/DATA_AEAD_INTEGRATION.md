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

`logout` clears the in-memory Data key. File and directory handles from the
previous session are rejected after a later login. A failed desktop start and
the reboot command also clear the key. These checks do not encrypt Data.

The v2 account record wraps a random 32-byte Data key. Only successful
`account_authenticate` makes it available in RAM. The record is on the Data
volume in plaintext, so credential paths need a narrow raw-file exception
before login. No key stored on that medium can establish freshness against
an offline attacker. Boot verification, operator enrollment, and an external
rollback floor are separate prerequisites for a complete claim.

The record now has distinct migration-in-progress and encrypted-complete
flags. The account helper can advance those states while preserving the same
wrapped Data key, then revokes the current session. This kernel refuses login
from either state until encrypted-tree recovery is connected; setting a flag
alone does not activate encryption. Password rotation can rewrap the same key
in either state without unlocking Data. Account deletion still refuses those
states because it cannot yet verify that the encrypted tree is empty.

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
4 KiB plaintext chunk. Version 2 authenticates a stable file ID and a
generation. Each content revision uses a new random revision ID and nonce for
every chunk. Version 1 remains readable for the existing host fixtures. A
complete old file or disk snapshot can still be rolled back. Names, directory
layout, physical sizes, freed plaintext clusters, and SSD remanence remain
visible.

`data_aead_rewrite.c` can stream an authenticated old file into a distinct
shadow with a partial write, sparse extension or truncate. It verifies every
old chunk, including truncated-away chunks, allocates a new random file ID
and nonces, and wipes its caller-owned bounded workspace. Host fault tests
exercise tampering, wrong keys, entropy refusal, and disk-full writes. It
also offers a bounded full-shadow readback verifier for use after the caller's
storage barrier and before publication; host negatives cover changed bytes,
wrong keys, wrong paths, and short reads. Readback cannot establish future
durability or freshness. The helper does not publish or recover the shadow,
preserve filesystem metadata, migrate
plaintext, or intercept the production VFS. Those remain required before
the Data namespace is encrypted.
For a rename it can authenticate the old path and seal a fresh revision bound
to the destination path. The caller still has to handle directory descendants,
open descriptions, metadata and interrupted namespace replacement.
The bounded logical range reader authenticates the header and each requested
chunk before copying plaintext; an error in a later chunk wipes the prefix it
copied during that call. It requires a stable held backend object across reads
and does not yet replace production `openrfsfs_read` or `pread`.
The plaintext conversion helper reads a stable legacy source into a distinct
encrypted shadow with bounded workspace and refuses short reads, disk-full
writes and entropy failure. It leaves the source untouched. The caller still
owns the durable publish/recovery protocol and must not set the authenticated
migration-complete record bit until every Data file is converted and verified.

`data_aead_slots.c` checks two authenticated index records and selects the
highest complete encrypted revision. It syncs and verifies a candidate before
writing the inactive index. A one-file conversion helper retires the plaintext
source only after the encrypted revision is published. Host tests cut the data
flush, index write, index flush, source removal, and source flush. The module
is not connected to the production VFS, directory traversal, account state,
or either filesystem backend. These tests do not show that normal Data files
are encrypted.

A malformed inactive index can be the result of a cut during its write. A raw
media attacker can make the same bytes and cause selection of the older
revision. The helper refuses an index whose complete shape has a bad MAC, and
it refuses conflicting authenticated generations, but it cannot distinguish
a torn write from a deliberate erasure. Detecting that rollback needs a
freshness value held outside the Data volume.

The current envelope caps each physical file at 16 MiB. A 16 MiB FAT32 file
and a 64 MiB ext4 mutable file cannot be represented as one encrypted file
within the respective backend limits. `data_aead_manifest.c` seals an
encrypted descriptor for up to eight 8 MiB plaintext segments, enough for a
64 MiB logical file. It authenticates the file identity, logical length,
segment revision IDs, lengths, generations, and segment binding paths. It
derives an opaque 8.3 physical path from the Data key and each revision ID.
The slot selector chooses the highest adjacent authenticated generation and
refuses a shaped record with a bad tag. It cannot detect erasure of a newer
slot without an external freshness value. Its publication helper syncs and
verifies each segment, writes and reads back a temporary manifest, removes the inactive
slot, renames the temp into that slot, then syncs and selects the result.
Host tests cut each of those callbacks and retry. `data_aead_backend.c` maps
them to FAT32 and ext4 backend operations, keeps segment handles open through
verification, and uses 8.3 storage paths. It also migrates one held plaintext
file: it derives repeatable staging IDs, writes each encrypted segment with
fresh random chunk nonces, syncs, verifies and publishes the manifest, then
removes the plaintext source. A retry checks the published manifest first;
if it is complete, it finishes source removal. Host tests cut each simulated
write, directory creation, sync, rename and removal boundary on both backend
styles, plus disk full, tampering, and a wrong key. Its read helper verifies
the manifest and segments, decrypts the requested range, and clears output
after an error. No production caller uses the adapter yet. It has not been
tested against raw FAT32 or ext4 images.
File and directory names in the legacy tree remain visible until a namespace
migration removes them. The helper alone does not protect Data.

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
Legacy plaintext has no authentication; a change made before the first trusted
inventory cannot be distinguished from the user's original bytes without a
prior trusted digest or an external trust root.
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
