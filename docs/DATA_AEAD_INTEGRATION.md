<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Encrypted Data storage

Ordinary boot installs the encrypted Data backend before starting the shell.
Shell commands, desktop Files, native file handles, and package file operations
reach Data through the same VFS mount. The fixed `OPENRFS` credential paths use
the physical backend so an account can be read before login. All other Data
paths stay locked until the account password authenticates, migration finishes,
and the namespace loads with the account's random Data key. There is no
plaintext read or write fallback. Logout and storage errors revoke handles and
wipe the in-memory key and namespace.

## Disk format

The v2 account record wraps a random 32-byte Data key. It stores a migration
state and a generation. Password changes rewrap the same key. The account
record, its fixed path, the username, KDF parameters, and record generations
are visible on the Data volume. The password and unwrapped Data key are not
stored there.

An encrypted namespace event log stores file and directory names, stable
object IDs, mode, owner, attributes, and timestamps. It is sealed in the
versioned Data AEAD format. FAT32 lookup folds ASCII case and displays names
in lowercase; ext4 preserves case. Physical storage paths are keyed opaque
8.3 names on both filesystems. Renaming a file or directory keeps its object
ID and does not move its ciphertext.

Regular file content uses Monocypher XChaCha20-Poly1305. A version 2 envelope
authenticates the stable file ID, chunk binding, length, generation, and fresh
nonce. A manifest authenticates the logical length and up to eight 8 MiB
segments. Every rewrite creates fresh revision IDs and nonces, verifies its
shadow, syncs it, and publishes one of two authenticated manifest slots.
Readers verify the manifest and ciphertext before returning plaintext. Failed
reads clear the caller's output. The maximum logical file size is 64 MiB.

The format leaves physical file sizes, the count and layout of opaque files,
access patterns, and old encrypted revisions visible. It does not encrypt the
System volume or the account record's public fields.

## Migration and recovery

After a successful password check, an account with legacy Data advances to
`MIGRATING`. A keyed, authenticated inventory records each source path,
content digest, and supported metadata before any source is removed. Each
regular file is copied to authenticated storage and checked before a namespace
event makes it visible. Only then is the plaintext source removed and synced.
Directories are removed after their contents. The account advances to
`ENCRYPTED` only after the inventory and final physical root census pass.
Login never publishes the key while this work is incomplete.

On restart, migration authenticates the inventory and namespace, verifies
remaining sources or completed ciphertext, and resumes. Disk full, damaged
records, wrong keys, unexpected files, and authentication failures stop login.
The old credential slot stays available until a newer account generation is
durable. Host tests cut every modeled write, sync, rename, and source removal
boundary on FAT32 and ext4 adapters.

Migration currently accepts regular files and directories with paths of at
most 255 bytes, at most 384 entries, and files of at most 64 MiB. FAT32's
physical backend can enumerate at most 64 entries in one directory. Symlinks,
hard-linked files, extended attributes, special files, and larger files are
refused before their source is retired. The large ext4 fixture exercises this
refusal. Link, symlink, and xattr mutations through the encrypted VFS are
refused. A supported file remains accessible through read, write, append,
offset write, truncate, rename, and delete after migration.

Account deletion requires an empty logical Data tree. It removes the current
credential wrap, leaving inaccessible ciphertext on the volume. A saved copy
of an old account record can restore an old wrap. A complete older volume can
also be replayed: detecting whole-volume rollback needs a freshness value
held outside the Data volume.

In-place migration cannot erase old plaintext blocks, directory slack,
journal copies, snapshots, or SSD remanence. A raw ext4 image can still contain
an old filename or file content after the live namespace is encrypted. Use a
freshly formatted medium and retire the old medium to remove those remnants
from the new medium. Legacy plaintext changed before the first trusted
inventory cannot be distinguished from original data without an earlier
trusted digest or external trust root.

## Checks

`make data-aead-backend-host-test` covers the format, namespace, migration
replay, tampering, wrong keys, disk full, and modeled power cuts.
`make encrypted-data-qemu-test` migrates a plaintext file, exercises shell and
desktop access, changes the password, checks logout and login, and scans FAT32
and ext4 raw images. `make encrypted-data-native-qemu-test` runs the native
file-handle probe and package upload through the encrypted mount.
`make encrypted-data-tamper-qemu-test` refuses altered ciphertext on both
filesystems. `make encrypted-data-powercut-qemu-test` cuts power during
migration and checks recovery. `make encrypted-data-diskfull-qemu-test` fills
each filesystem, checks write refusal and key revocation, removes the filler,
then reboots and checks the original file and raw image. The account migration
refusal targets check unsupported ext4 entries and extended attributes.
