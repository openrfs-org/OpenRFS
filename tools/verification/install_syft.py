#!/usr/bin/env python3
"""Install one digest-verified Syft Linux release binary."""

from __future__ import annotations

import argparse
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile
import urllib.request


VERSION = "1.52.0"
ARCHIVE = f"syft_{VERSION}_linux_amd64.tar.gz"
SHA256 = "caeedb81fb0491615f1ebd1761e4145d41ee86dd2cc7bf80669f9f5ad9d6133d"
URL = f"https://github.com/anchore/syft/releases/download/v{VERSION}/{ARCHIVE}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    archive = output / ARCHIVE
    if not archive.is_file():
        with urllib.request.urlopen(URL, timeout=120) as response:
            data = response.read(64 * 1024 * 1024 + 1)
        if len(data) > 64 * 1024 * 1024:
            raise RuntimeError("Syft release exceeds download limit")
        archive.write_bytes(data)
    data = archive.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise RuntimeError("Syft release digest mismatch")
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as members:
        member = members.extractfile("syft")
        if member is None:
            raise RuntimeError("Syft binary absent from verified release")
        binary = member.read(160 * 1024 * 1024 + 1)
    if len(binary) > 160 * 1024 * 1024:
        raise RuntimeError("Syft binary exceeds extraction limit")
    executable = output / "syft"
    executable.write_bytes(binary)
    executable.chmod(0o755)
    version = subprocess.check_output([str(executable), "--version"], text=True).strip()
    if version != f"syft {VERSION}":
        raise RuntimeError(f"unexpected Syft version: {version}")
    print(f"{version} {ARCHIVE} sha256:{SHA256}")


if __name__ == "__main__":
    main()
