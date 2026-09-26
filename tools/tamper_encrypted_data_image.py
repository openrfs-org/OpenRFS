#!/usr/bin/env python3
"""Change encrypted segment bytes without changing filesystem metadata."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess

import fat32_image


def ext4_listing(image, path):
    result = subprocess.run(("debugfs", "-R", f"ls -p {path}", str(image)),
                            capture_output=True, text=True, check=True)
    for line in result.stdout.splitlines():
        parts = line.strip().split("/")
        if len(parts) < 7 or parts[5] in (".", "..", ""):
            continue
        yield parts[5], parts[2].startswith("04")


def ext4_segments(image):
    roots = [name for name, directory in ext4_listing(image, "/")
             if directory and re.fullmatch(r"[0-9A-F]{8}", name)]
    if len(roots) != 1:
        raise RuntimeError(f"expected one keyed Data root, got {roots!r}")
    pending = ["/" + roots[0]]
    while pending:
        parent = pending.pop()
        for name, directory in ext4_listing(image, parent):
            path = parent + "/" + name
            if directory:
                pending.append(path)
            elif name == "SEG.DAT":
                result = subprocess.run(("debugfs", "-R", f"blocks {path}",
                                         str(image)), capture_output=True,
                                        text=True, check=True)
                blocks = result.stdout.split()
                if not blocks:
                    raise RuntimeError(f"no blocks for {path}")
                yield int(blocks[0]) * 4096 + 120


def fat32_segments(image):
    raw = image.read_bytes()
    geometry = fat32_image.parse_geometry(raw)
    entries = fat32_image.inspect_image(raw)["files"]
    for entry in entries:
        if (not entry["directory"] and entry["path"].endswith("/SEG.DAT")
                and entry["size"] > 120):
            yield geometry.sector_offset(
                geometry.cluster_sector(entry["first_cluster"])) + 120


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--filesystem", choices=("fat32", "ext4"), required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("source and output must differ")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.source, args.output)
    offsets = list((fat32_segments if args.filesystem == "fat32" else
                    ext4_segments)(args.output))
    if not offsets:
        raise RuntimeError("encrypted Data has no segments to tamper")
    with args.output.open("r+b") as image:
        for offset in offsets:
            image.seek(offset)
            old = image.read(1)
            if len(old) != 1:
                raise RuntimeError(f"missing byte at {offset}")
            image.seek(offset)
            image.write(bytes((old[0] ^ 0x80,)))
    print(f"tampered {len(offsets)} authenticated segments")


if __name__ == "__main__":
    main()
