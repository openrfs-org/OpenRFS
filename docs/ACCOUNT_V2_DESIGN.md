<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Account v2 boundary and migration design

This records the implemented credential format and the remaining Data security
work. The credential change alone makes no Data confidentiality claim.

## Production v2 stage

`account_create`, `account_authenticate`, and the shell's `useradd`/`starty`
now use a 224-byte v2 record in `OPENRFS/LOGIN.V2A`. A `passwd` prompt calls
`account_change_password`. The record encodes only the supported Argon2id
v1.3, 64 MiB, three-pass, four-lane tuple. Parsing rejects malformed lengths,
reserved fields, names and unsupported parameters before KDF work. A SHA-256
checksum screens accidental corruption; it is explicitly not an attacker
authenticator. Argon2id output is expanded under separate BLAKE2b labels for
the verifier and the XChaCha20-Poly1305 Data-key wrap. The wrap authenticates
the full header, verifier, nonce and generation. The random 32-byte Data key
is held only after successful login and is wiped after a failed login.

A successful v1 login stages and syncs v2 before unlinking v1. If staging
fails, login refuses and v1 remains for retry. Password change writes the
inactive v2 slot at a higher generation, syncs it, reopens and authenticates
it, then retires the previous slot. A cut before the new slot's rename leaves
the old password usable; a cut after selects the new generation. The host
test injects a pre-rename sync failure and a post-commit unlink failure,
checking that the old password survives the first and the new password works
after the second. An additional host test injects a malformed or short inactive
slot alongside a valid slot; login authenticates the intact wrap and removes
the damaged peer before exposing the Data key. An I/O error, two malformed
slots, or a single malformed slot still fail closed. This does not simulate a
power cut during FAT32's rename itself or establish an external rollback floor.
The shell and desktop capture exercised production v2 creation, password
change and login in QEMU. Deletion is not
exposed while Data files remain plaintext and the key is not used by VFS.

This is not crash-safe re-encryption of Data. Password change rewraps the same
Data key; old wraps can remain in filesystem or media history. Whole-volume
rollback can restore a retired password because no external generation floor
is enforced. The record and names are plaintext metadata, and the current
Data read/write path still stores content in plaintext. These limits must be
closed before the milestone or a confidentiality claim.

## Production path and current stage

The shell's `useradd` and `starty` prompts own bounded password buffers and
call `account_create` and `account_authenticate`. Those functions read and
write `OPENRFS/LOGIN.DAT` on the writable Data volume through `openrfsfs_*`.
Creation obtains its salt from `random_bytes`; persistence writes
`OPENRFS/LOGIN.NEW`, syncs, renames and syncs again. The record, username,
password input, filesystem bytes and interrupted writes are attacker-controlled
at this boundary. The v1 checksum detects accidents but is not an
authentication key. The shell is the only present login caller.

The first bounded change adds an online throttle at `account_authenticate`:
after the third invalid attempt, delays grow from one second to at most 60
seconds. It uses the monotonic clock and fails closed when that clock is not
running. A successful login clears the count. The throttle spans username
guesses, remains in kernel memory only, and resets on reboot. It cannot resist
offline guessing of a stolen v1 record or an attacker who can reboot at will.
It must remain in the production login path as v2 replaces v1.

## v2 verifier and resource gate

Use the already vendored, unmodified Monocypher 4.0.3 `crypto_argon2` with
Argon2id. The sources are recorded as byte-for-byte upstream in
`vendor/monocypher/OPENRFS-PORT.md` and are dual BSD-2-Clause/CC0 licensed in
`vendor/monocypher/LICENCE.md`. The RFC 9106 memory-constrained profile is
64 MiB, three passes, four lanes, a 16-byte salt and a 32-byte output:
<https://datatracker.ietf.org/doc/html/rfc9106#section-7.4>. Monocypher's lanes are
computed single-threaded. A v2 parser must accept only an explicitly encoded,
supported parameter tuple; reject zero, noncanonical, overflowed and
excessive values before any allocation or KDF work. Use published Argon2id
vectors and an independent implementation for differential checks.

The current general-purpose heap is only 16 MiB (`HEAP_SIZE`), so it cannot
honestly host that 64 MiB work area. A dedicated, guarded, single-owner arena
or a carefully audited heap expansion must precede v2 activation. The arena
must account for physical frames and CPU time, prevent concurrent KDFs from
overcommitting, wipe the work area, and refuse login if the promised memory is
unavailable. Do not silently lower the recorded parameters to fit a QEMU
fixture. The present 128 MiB networking VM profile is not proof that the
account/desktop profile has spare physical memory.

### Bounded KDF dependency stage

`account_kdf_v2_derive` now fixes the only supported tuple to Argon2id v1.3,
64 MiB, three passes, four lanes, a 16-byte salt and a 32-byte output. It
reuses the vendored Monocypher implementation. Its single-owner supervisor
arena maps one 64 MiB contiguous frame allocation below 1 GiB at a separate
virtual address, with unmapped guard pages and 16 MiB of frames left in
reserve. It holds scheduler preemption while the mapping exists so a process
address space cannot be built with a transient supervisor mapping, and it
requires the live kernel address space on entry, while hardware interrupts
remain enabled during the KDF. It wipes the entire work
area before unmapping and refuses output if allocation, mapping, or cleanup
fails. A partial map rollback or arena unmap failure is fail-stop so scheduling
cannot copy a live supervisor mapping into a process address space. A
frame-release failure keeps
ownership rather than recycling reachable frames. Physical fragmentation or
low memory causes an explicit refusal.

The host test checks the published RFC 9106 Argon2id vector, compares the
64 MiB profile with independent `libargon2`, and exercises parameter and
resource refusal. The counted `account-kdf` QEMU scenario calls the real KDF in
the 128 MiB guest, checks the independently derived output, and verifies arena
cleanup. The production credential path now calls it. The desktop capture
exercises v2 creation and login with the real KDF; host tests cover parser
and sync-failure behavior. Power-cut media recovery remains open.

Derive independent verifier and key-encryption material from the Argon2id
result with distinct, versioned labels. Do not persist the raw result or use
the stored verifier itself as a Data key. A new random Data encryption key is
wrapped with authenticated encryption under the password-derived wrapping
key; its nonce, record identity, parameter tuple and generation belong in the
authenticated associated data. A keyed verifier and AEAD tag distinguish a
wrong password from corrupted persistent state without returning a key.

## Migration and crash boundary

Do not silently replace a v1 record at login. First authenticate the v1
credential through the production entry point, allocate and validate v2
resources, and establish the encrypted Data-key state. Then write a staged v2
record with generation and explicit migration state; sync it, atomically
switch the active record, and sync the directory/volume. Password change
should stage a second authenticated wrap for the same Data key so a power cut
at every durable transition leaves at least one known credential able to
unlock it. Only retire the old wrap after the new record and its Data-key wrap
are durable and verified on reopen. Test disk full and power loss before and
after each sync/rename, then boot and authenticate both expected credentials
at each state. Deletion must define what happens to encrypted Data.

The Data volume is currently plaintext. Migration needs its own resumable
transaction and raw-media residue check before confidentiality at rest can be
claimed. The same writable volume cannot provide freshness: whole-volume or
record rollback can restore an old password wrap. Password revocation and a
rollback floor require an operator-controlled external state root. FAT32,
ext4, snapshots and SSD wear levelling cannot guarantee erasure by overwrite.
