<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Account v2 and Data unlock

The 224-byte account record lives in `OPENRFS/LOGIN.V2A` or `LOGIN.V2B` on
the Data volume. It records a username, generation, migration state, fixed
Argon2id parameters, verifier, and an authenticated wrap of a random 32-byte
Data key. The username, record paths, generation, and KDF parameters are
visible on disk. The key is unwrapped only after password authentication.

The only accepted KDF tuple is Argon2id v1.3 with 64 MiB, three passes, four
lanes, a 16-byte salt, and a 32-byte result. Production uses the vendored
Monocypher 4.0.3 implementation. The parser rejects malformed records and
unsupported parameters before KDF work. Separate BLAKE2b labels derive the
verifier and XChaCha20-Poly1305 wrapping key. The wrap authenticates the
record header, verifier, nonce, and generation. A SHA-256 checksum detects
accidental record damage; the AEAD tag authenticates the record. The 64 MiB
single-owner KDF arena has guard pages and is wiped on release. Login refuses
when that allocation or the monotonic clock is unavailable.

Login holds the Data volume locked until the password, selected account slot,
encrypted namespace, and any pending migration have been checked. The
account flags advance from legacy plaintext to `MIGRATING` to `ENCRYPTED`.
Each transition stages, syncs, reopens, and authenticates a newer record
before retiring the previous slot. Migration keeps a keyed inventory and
resumes after interruption. The Data key is published to the VFS only after
the final account record and encrypted Data state verify. A damaged record,
wrong password, tampered Data, I/O error, or incomplete migration refuses
the unlock. Logout and storage errors revoke open handles and wipe the key.

A successful v1 login first stages an authenticated v2 record and retains
the v1 credential until it is durable. Password change writes the inactive
v2 slot with a higher generation and a new wrap of the same Data key. It
verifies that slot on reopen before removing the old one. A failure revokes
the current Data session, including when the newer record committed but old
slot cleanup remains. The next login selects and cleans up the valid slot.
Reusing the Data key lets existing files remain readable after a password
change. Old record copies can still wrap that key.

`userdel` authenticates the password and requires an empty logical Data
namespace. It deletes the current credential record and wipes the session.
Ciphertext and old record copies may remain on the medium. A saved account
record can restore an earlier password wrap. Whole-volume rollback and
password revocation need a trusted generation or freshness value outside the
Data volume.

After three invalid attempts, online login delays grow from one second to
at most 60 seconds. The counter is held in memory and resets on reboot. It
does not prevent offline guessing of a stolen record.

`DATA_AEAD_INTEGRATION.md` describes the encrypted namespace, file format,
migration limits, and raw-media residue. Host tests exercise account slot
faults and modeled migration cuts. FAT32 and ext4 QEMU tests exercise login,
Data access, password change, logout, and deletion.
