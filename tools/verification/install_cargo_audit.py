#!/usr/bin/env python3
"""Install a digest-verified RustSec cargo-audit Linux release binary."""

from __future__ import annotations

import argparse
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile
import urllib.request


VERSION = "0.22.2"
ARCHIVE = f"cargo-audit-x86_64-unknown-linux-gnu-v{VERSION}.tgz"
SHA256 = "ab28a1bdb54db4d5d8ad5981cf1f959410370b3d28250dbd35f6a44248620e39"
URL = f"https://github.com/rustsec/rustsec/releases/download/cargo-audit/v{VERSION}/{ARCHIVE}"
MEMBER = f"cargo-audit-x86_64-unknown-linux-gnu-v{VERSION}/cargo-audit"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    archive = output / ARCHIVE
    if not archive.is_file():
        with urllib.request.urlopen(URL, timeout=60) as response:
            data = response.read(16 * 1024 * 1024 + 1)
        if len(data) > 16 * 1024 * 1024:
            raise RuntimeError("cargo-audit release exceeds download limit")
        archive.write_bytes(data)
    data = archive.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise RuntimeError("cargo-audit release digest mismatch")
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as members:
        member = members.extractfile(MEMBER)
        if member is None:
            raise RuntimeError("cargo-audit binary absent from verified release")
        binary = member.read(32 * 1024 * 1024 + 1)
    if len(binary) > 32 * 1024 * 1024:
        raise RuntimeError("cargo-audit binary exceeds extraction limit")
    executable = output / "cargo-audit"
    executable.write_bytes(binary)
    executable.chmod(0o755)
    version = subprocess.check_output([str(executable), "--version"],
                                      text=True).strip()
    if version != f"cargo-audit {VERSION}":
        raise RuntimeError(f"unexpected cargo-audit version: {version}")
    print(f"{version} {ARCHIVE} sha256:{SHA256}")


if __name__ == "__main__":
    main()
