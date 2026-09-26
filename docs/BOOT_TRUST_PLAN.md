<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Boot trust plan and current boundary

## Current path

The current build produces a fixed-address Multiboot2 ELF. `grub-mkrescue`
copies that ELF and `grub/grub.cfg` into an ISO; the configuration invokes
`multiboot2 /boot/openrfs.elf`. GRUB and the configuration load the kernel
before any OpenRFS Boot Ledger, self-test, or TPM code runs. No externally
authorized signature or rollback floor is enforced by that path. The repository
does not currently provide a native UEFI boot application or a proven
Secure Boot chain for this Multiboot2 handoff.

## Detached artifact verifier

`tools/boot_artifact_signature.py` is an **external, pre-boot tool**. It
signs a canonical manifest of the exact kernel ELF, GRUB configuration, and
any specified boot modules with Ed25519. The signing private key is supplied
as a path at execution time and is not stored in the repository, image, or CI.
Verification requires a public key and minimum generation supplied by an
operator-controlled source outside the disk being checked. It rejects a missing
artifact, changed bytes, bad or truncated signature, unknown manifest format,
noncanonical fields, or generation below that external floor. Multiple
operator-supplied public keys permit a staged key rotation; removing a key
from that trusted set revokes it. If the key set or floor resides on the same
attacker-controlled disk, rollback and substitution remain possible.

The verifier is reviewable and testable today, but the current GRUB boot does
**not call it**. A successful host verification followed by an attacker
replacing the ISO is outside the tool's guarantee. It does not constitute
enforcing image authenticity.

Example using an operator-held signing key and public key:

```sh
python3 tools/boot_artifact_signature.py sign \
  --private-key /operator/key.pem --public-key /operator/key.pub.pem \
  --generation 7 --manifest /operator/boot.manifest \
  --signature /operator/boot.manifest.sig \
  --artifact boot/openrfs.elf=build/openrfs.elf \
  --artifact boot/grub/grub.cfg=grub/grub.cfg

python3 tools/boot_artifact_signature.py verify \
  --public-key /operator/key.pub.pem --min-generation 7 \
  --manifest /operator/boot.manifest \
  --signature /operator/boot.manifest.sig \
  --artifact boot/openrfs.elf=build/openrfs.elf \
  --artifact boot/grub/grub.cfg=grub/grub.cfg
```

The example generation is illustrative. The operator must maintain its real
floor outside the writable boot and Data media. `make
boot-artifact-signature-test` generates disposable test keys, signs fixtures,
mutates kernel and configuration bytes, advances the floor, substitutes a
different key, corrupts and truncates the signature, and verifies that each
case is refused before restoring a passing case.

## Enforcing chain still to build

An automatic boot policy needs an authority the disk attacker cannot edit.
For UEFI deployment, that means controlled firmware variables and an enrolled
signing key, a signed and constrained first EFI stage, authenticated
configuration and boot modules, verification of the exact OpenRFS ELF before
handoff, and an externally protected rollback floor. The present GRUB
`multiboot2` command must be tested under the chosen Secure Boot policy;
firmware accepting a signed GRUB image alone does not prove that GRUB refuses
an altered configuration or kernel. BIOS deployments need an operator-held
external verifier and controlled boot media, or another independent root.

The enrollment test must use disposable OVMF variables and a disposable key.
It must show refusal of unsigned, modified, and old kernel and configuration
artifacts while preserving desktop and native boot paths. Production signing
keys must never enter the image, repository, CI job, or prompt. Rotation needs
an overlap period in the operator trust store and a separately exercised
revocation step. A rescue image needs its own signed manifest and an explicit
floor exception authorized by the operator; an old vulnerable rescue image
must not silently bypass the floor. Expiry by wall clock is meaningful only
where that clock has an external trusted source.

TPM measurements, if added, must come from a platform service after the image
has been verified by an external authority. PCR values and event order need
independent expectations and a guest that actually booted. A quote emitted
by unverified kernel code cannot authenticate that kernel. The current
detached verifier is not a TPM service and makes no hardware claim.
