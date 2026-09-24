<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Security port status

This page records the state of an **unfinished draft**. It is not a release
claim. The reference code came from the private
`saudaljuaid/openrfssecurity` tree at
`b95fd76559ca5ce277920c6f29506a7d4c4f5ac1`; this branch started from
OpenRFS `b6a4376fceea2342bbeade33017ae0365ac485ea`.
The reference's OpenGAT names and older OS assumptions are not authority for
current OpenRFS. No reference README or descriptive page was copied here.

## Source and target ledger

| Area | Reference | OpenRFS at branch base | State in this draft |
| --- | --- | --- | --- |
| SHA-256 | Standalone GPL-3.0-only implementation | `package_state_sha256*` already linked | Existing implementation reused |
| HMAC, HKDF, HMAC_DRBG, health tests | GPL-3.0-only reference and published vectors | Xoshiro/splitmix for ordinary random output | Adapted into the production `random.c` path |
| AEAD | Vendored Monocypher 4.0.3, BSD-2-Clause OR CC0-1.0 | Monocypher already linked | No duplicate copied |
| Credential records and Argon2id | Reference v2 design and parsers | v1 iterated SHA-256 account record | Per-boot login backoff and bounded Argon2id KDF added; v2 record **not ported** |
| Vault and Data file migration | Reference design and tests | Plaintext FAT32/ext4 Data paths through VFS | **Not ported** |
| Kernel authenticity | Reference acknowledges no image verification | Unsigned GRUB Multiboot2 image and configuration | Detached host verifier added; **not enforcing at boot** |
| TPM and rollback | Reference measurement model | No external freshness authority | **Not integrated** |
| Process and package controls | Reference proposals | Current native and package paths | **Not audited to completion** |

The v1 login backoff starts after three failed attempts, caps at 60 seconds,
and requires the monotonic clock. It resets on reboot and cannot slow offline
guessing of a stolen record. `docs/ACCOUNT_V2_DESIGN.md` records the v2 memory,
key, persistence and migration gates. The 64 MiB Argon2id work arena has an
independent host result and a counted 128 MiB QEMU scenario, but no production
credential record calls it yet. This is not a credential v2 claim.

The branch now also contains OpenRFS's driver-layer merge at
`f78d25d4ac43f05875bce17d80fbba738428d61e`. Its network device selector
is reached only after the entropy capability check. The driver QEMU runner and
interactive driver command select an entropy-capable CPU model. CI boots
the e1000 path with both CPU RNG instructions disabled, requires the exact
entropy refusal and guest failure exit, then boots it again with CPU entropy
and requires the driver network pass. These are model tests, not
DMA isolation evidence. OpenRFS has no enforced IOMMU boundary here: a
malicious bus-mastering device can read or overwrite reachable physical memory,
including kernel state and secrets, regardless of a driver's intended buffer
ownership. Driver claimant, interrupt teardown, and hostile-device response
audits remain open.

The reference's own C and Rust sources identify as GPL-3.0-only. Its Monocypher
copy is dual licensed BSD-2-Clause OR CC0-1.0; OpenRFS already has that release.
The licence and provenance review must be repeated at the exact final head.

## Enforced entropy boundary

`random_bytes` and `random_strong_bytes` now use one HMAC_DRBG-SHA-256
instance. The HMAC uses OpenRFS's existing `package_state_sha256*` engine.
There is no timestamp, zero, xoshiro, or virtio-rng fallback. RDSEED is chosen
when the CPU advertises it; RDRAND is used only when RDSEED is absent. If neither
exists, both byte APIs refuse output. If a selected instruction fails, a health
test rejects a sample, or reseeding fails, the DRBG is wiped and remains locked
out until reboot.

Instantiation and reseeding draw 64 64-bit source samples, apply a repetition
count cutoff of five and an adaptive proportion cutoff of eight matches to the
window reference in 512 samples, then derive 48 seed bytes through
HKDF-HMAC-SHA-256. One request is capped at 256 bytes. A reseed is required
after 1,048,576 generate requests. The single-core kernel disables interrupts
while changing DRBG state. The status structure reports the enforced state and
issued-byte count.

Callers now handle refusal: network identifiers do not get a zero or fixed
substitute, network initialization refuses when entropy is absent, account
creation returns its random-unavailable status, the native random syscall
returns an error, and the network syscall reports an entropy error rather than
a bad pointer. The native syscall wipes its transfer buffer after use. These
changes do not make existing credential records or Data files encrypted.

CPU entropy is a platform trust assumption. These health checks detect some
failures, including a stuck source; they do not measure physical min-entropy,
detect a malicious hypervisor, or establish a physical-hardware guarantee.
QEMU's CPU options test available and absent behavior only.

## Reproduction so far

```sh
make random-host-test
make kernel
make entropy-qemu-test
make boot-artifact-signature-test
```

The host test uses RFC 4231 HMAC and NIST CAVP HMAC_DRBG vectors, checks the
HKDF result against Python's independent `hmac`/`hashlib`, then injects no
entropy, a repeated source, a biased source, a failed source, and a failed
reseed. The QEMU gate boots the same image with `-cpu max`,
`-cpu max,-rdseed`, and `-cpu max,-rdrand,-rdseed`, using a modern virtio NIC.
It requires normal guest completion and the corresponding initialization or refusal
marker. A timeout is a failure.

The wider `make verify`, `make qemu-tests`, migration, power-cut, media
inspection, hardware, and independent-review gates remain open.

## Boot and storage decisions still required

The current ISO rule copies `openrfs.elf` and `grub.cfg` into a GRUB image.
The Multiboot2 configuration loads the kernel without an external signature
check. A hash or TPM quote produced by that kernel cannot authenticate it.
The detached verifier and the missing external chain are mapped in
[`BOOT_TRUST_PLAN.md`](BOOT_TRUST_PLAN.md).
Enforcing image authenticity requires an operator-controlled firmware or
external verification chain covering GRUB, configuration, kernel, and modules,
plus a rollback floor and recovery image. No such chain is claimed here.

The current Data namespace uses VFS over FAT32 or ext4, and native handles and
package upload paths call those production file operations. File content
encryption needs a versioned abstraction shared by all those operations, with
stable file identity, authenticated metadata, crash transactions, and a
resumable plaintext migration. No production file path is encrypted by this
draft. Existing deleted or rewritten plaintext can remain on physical media.
Whole-volume rollback remains possible without an external freshness root.

No qualified independent cryptography or OS-security review has occurred.
Physical hardware, firmware enrollment, metadata leakage, deleted-data
residue, whole-disk rollback, and a malicious running kernel remain outside
the demonstrated boundary.
