#!/usr/bin/env python3
"""Install a digest-verified OSV-Scanner Linux release binary."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import urllib.request


VERSION = "2.6.0"
ASSET = "osv-scanner_linux_amd64"
SHA256 = "ca69b3d3cd08f889a49dc0a383122f71cc528b83803671df5fd874d97485b108"
URL = f"https://github.com/google/osv-scanner/releases/download/v{VERSION}/{ASSET}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    executable = output / "osv-scanner"
    if not executable.is_file():
        with urllib.request.urlopen(URL, timeout=120) as response:
            data = response.read(80 * 1024 * 1024 + 1)
        if len(data) > 80 * 1024 * 1024:
            raise RuntimeError("OSV-Scanner release exceeds download limit")
        executable.write_bytes(data)
    if hashlib.sha256(executable.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError("OSV-Scanner release digest mismatch")
    executable.chmod(0o755)
    version = subprocess.check_output([str(executable), "--version"], text=True)
    if version.splitlines()[0] != f"osv-scanner version: {VERSION}":
        raise RuntimeError(f"unexpected OSV-Scanner version: {version}")
    print(f"osv-scanner {VERSION} {ASSET} sha256:{SHA256}")


if __name__ == "__main__":
    main()
