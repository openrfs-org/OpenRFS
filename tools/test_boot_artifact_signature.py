#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Negative controls for the detached, external boot-artifact verifier."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile

TOOL = pathlib.Path(__file__).with_name("boot_artifact_signature.py")


def command(*arguments: str, accepted: bool) -> None:
    result = subprocess.run(
        [sys.executable, str(TOOL), *arguments],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        check=False,
    )
    if accepted and result.returncode != 0:
        raise RuntimeError(f"expected acceptance: {result.stderr}")
    if not accepted and (
        result.returncode == 0 or "REFUSED:" not in result.stderr
    ):
        raise RuntimeError(
            f"expected explicit refusal: {result.stdout} {result.stderr}"
        )


def sign_payload(private: pathlib.Path, manifest: pathlib.Path,
                 signature: pathlib.Path, payload: bytes) -> None:
    manifest.write_bytes(payload)
    with signature.open("wb") as output:
        subprocess.run(
            ["openssl", "pkeyutl", "-sign", "-rawin", "-inkey",
             str(private), "-in", str(manifest)],
            stdout=output, check=True,
        )


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="openrfs-boot-signature-") as room:
        base = pathlib.Path(room)
        private = base / "test-key.pem"
        public = base / "test-pub.pem"
        other_private = base / "other-key.pem"
        other_public = base / "other-pub.pem"
        kernel = base / "openrfs.elf"
        config = base / "grub.cfg"
        manifest = base / "manifest"
        signature = base / "signature"
        kernel.write_bytes(b"OpenRFS kernel test bytes\x00\xff")
        config.write_bytes(b"multiboot2 /boot/openrfs.elf\n")
        for secret, visible in (
            (private, public), (other_private, other_public)
        ):
            subprocess.run(
                ["openssl", "genpkey", "-algorithm", "ED25519",
                 "-out", str(secret)], check=True,
                stdout=subprocess.DEVNULL,
            )
            subprocess.run(
                ["openssl", "pkey", "-in", str(secret), "-pubout",
                 "-out", str(visible)], check=True,
                stdout=subprocess.DEVNULL,
            )
        items = [
            "--artifact", f"boot/openrfs.elf={kernel}",
            "--artifact", f"boot/grub/grub.cfg={config}",
        ]
        sign = [
            "sign", "--private-key", str(private),
            "--public-key", str(public), "--generation", "7",
            "--manifest", str(manifest), "--signature", str(signature),
            *items,
        ]
        verify = [
            "verify", "--public-key", str(public),
            "--min-generation", "7", "--manifest", str(manifest),
            "--signature", str(signature), *items,
        ]
        command(*sign, accepted=True)
        command(*verify, accepted=True)
        original_kernel = kernel.read_bytes()
        kernel.write_bytes(original_kernel[:-1] + bytes([original_kernel[-1] ^ 1]))
        command(*verify, accepted=False)
        kernel.write_bytes(original_kernel)
        command(*verify, accepted=True)
        original_config = config.read_bytes()
        config.write_bytes(original_config + b"# modified\n")
        command(*verify, accepted=False)
        config.write_bytes(original_config)
        command(*verify, accepted=True)
        command(
            *[("8" if value == "7" and index == 4 else value)
              for index, value in enumerate(verify)],
            accepted=False,
        )
        command(*verify, accepted=True)
        command(
            *[str(other_public) if value == str(public) else value
              for value in verify],
            accepted=False,
        )
        command(*verify, accepted=True)
        original_signature = signature.read_bytes()
        signature.write_bytes(
            bytes([original_signature[0] ^ 1]) + original_signature[1:]
        )
        command(*verify, accepted=False)
        signature.write_bytes(original_signature[:-1])
        command(*verify, accepted=False)
        signature.write_bytes(original_signature)
        command(*verify, accepted=True)
        original_manifest = manifest.read_bytes()
        prefix, encoded = original_manifest.split(b"\n", 1)
        document = json.loads(encoded)
        signed_invalid = [
            original_manifest[:-1] + b' ',
            original_manifest[:-2] + b',"format":1}\n',
            prefix + b"\n" + json.dumps(
                {**document, "format": 2}, sort_keys=True,
                separators=(",", ":"),
            ).encode("ascii") + b"\n",
            prefix + b"\n" + json.dumps(
                {**document, "unknown": 1}, sort_keys=True,
                separators=(",", ":"),
            ).encode("ascii") + b"\n",
        ]
        for payload in signed_invalid:
            sign_payload(private, manifest, signature, payload)
            command(*verify, accepted=False)
        manifest.write_bytes(b"X" * 65537)
        command(*verify, accepted=False)
        manifest.write_bytes(original_manifest)
        signature.write_bytes(b"X" * 65)
        command(*verify, accepted=False)
        signature.write_bytes(original_signature)
        command(*verify, accepted=True)
        print("boot artifact signature positive and negative controls passed")


if __name__ == "__main__":
    main()
