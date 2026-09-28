#!/usr/bin/env python3
"""Install the pinned cargo-fuzz release after archive digest verification."""

from __future__ import annotations

import argparse
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile
import urllib.request


VERSION = "0.13.2"
ARCHIVE = f"cargo-fuzz-{VERSION}-x86_64-unknown-linux-musl.tar.gz"
SHA256 = "b5b704018b63e0f151c17a057ac53b5111e1db545d1b9f72fee79f08a545931c"
URL = f"https://github.com/rust-fuzz/cargo-fuzz/releases/download/{VERSION}/{ARCHIVE}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    archive = output / ARCHIVE
    if not archive.is_file():
        with urllib.request.urlopen(URL, timeout=60) as response:
            data = response.read(8 * 1024 * 1024 + 1)
        if len(data) > 8 * 1024 * 1024:
            raise RuntimeError("cargo-fuzz release exceeds download limit")
        archive.write_bytes(data)
    data = archive.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise RuntimeError("cargo-fuzz release digest mismatch")
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as members:
        member = members.extractfile("cargo-fuzz")
        if member is None:
            raise RuntimeError("cargo-fuzz binary absent from verified release")
        binary = member.read(8 * 1024 * 1024 + 1)
    if len(binary) > 8 * 1024 * 1024:
        raise RuntimeError("cargo-fuzz binary exceeds extraction limit")
    executable = output / "cargo-fuzz"
    executable.write_bytes(binary)
    executable.chmod(0o755)
    version = subprocess.check_output([str(executable), "--version"],
                                      text=True).strip()
    if version != f"cargo-fuzz {VERSION}":
        raise RuntimeError(f"unexpected cargo-fuzz version: {version}")
    print(f"{version} {ARCHIVE} sha256:{SHA256}")


if __name__ == "__main__":
    main()
