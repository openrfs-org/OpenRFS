#!/usr/bin/env python3
"""Install exact Linux actionlint/zizmor release assets after SHA-256 verification."""

from __future__ import annotations

import argparse
import hashlib
import io
from pathlib import Path
import tarfile
import urllib.request


RELEASES = (
    ("actionlint", "v1.7.12", "actionlint_1.7.12_linux_amd64.tar.gz",
     "8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8",
     "https://github.com/rhysd/actionlint/releases/download/"),
    ("zizmor", "v1.30.1", "zizmor-x86_64-unknown-linux-gnu.tar.gz",
     "e65324f4430c2717591937edcec90ccbefaf14c174f8ec9415e03ca875b46e1a",
     "https://github.com/zizmorcore/zizmor/releases/download/"),
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for name, version, archive_name, expected, prefix in RELEASES:
        archive = output / archive_name
        if not archive.is_file():
            with urllib.request.urlopen(prefix + version + "/" + archive_name,
                                        timeout=60) as response:
                data = response.read(32 * 1024 * 1024 + 1)
            if len(data) > 32 * 1024 * 1024:
                raise RuntimeError(f"{name} release exceeds download limit")
            archive.write_bytes(data)
        data = archive.read_bytes()
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected:
            raise RuntimeError(f"{name} release digest mismatch: {actual}")
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as members:
            binary = members.extractfile(name)
            if binary is None:
                raise RuntimeError(f"{name} binary missing from verified archive")
            contents = binary.read(32 * 1024 * 1024 + 1)
            if len(contents) > 32 * 1024 * 1024:
                raise RuntimeError(f"{name} binary exceeds extraction limit")
        executable = output / name
        executable.write_bytes(contents)
        executable.chmod(0o755)
        print(f"{name} {version} {archive_name} sha256:{expected}")


if __name__ == "__main__":
    main()
