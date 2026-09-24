<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Account v2 boundary and migration design

This is a design record, not a v2 implementation or a confidentiality claim.

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
