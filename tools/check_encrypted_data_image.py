#!/usr/bin/env python3
"""Check visible Data paths and scan raw bytes after a QEMU migration."""

import argparse
import json
from pathlib import Path
import re
import subprocess

import fat32_image


OLD = (b"OPENRFS_PLAINTEXT_SENTINEL_9466", b"SECRET.TXT")
NEW = (b"HIDDEN.TXT", b"VAULT", b"NOTE.TXT", b"CLOSED",
       b"PERF.BIN", b"A.TXT", b"B.TXT",
       b"OPENRFS_UPLOAD_SENTINEL_3924",
       b"amber\n", b"blue\n", b"gold\n", b"nested\n")


def occurrences(raw, needle):
    found = []
    at = 0
    while True:
        at = raw.find(needle, at)
        if at < 0:
            return found
        found.append(at)
        at += len(needle)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--filesystem", choices=("fat32", "ext4"), required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    raw = args.image.read_bytes()
    if args.filesystem == "fat32":
        parsed = fat32_image.inspect_image(raw)
        names = [entry["path"] for entry in parsed["files"]
                  if "/" not in entry["path"]]
    else:
        listing = subprocess.run(("debugfs", "-R", "ls -p /",
                                  str(args.image)), capture_output=True,
                                 text=True, check=True).stdout
        names = [line.strip("/\n").split("/")[4] for line in listing.splitlines()
                 if line.startswith("/")]
    names = [name for name in names if name not in (".", "..")]
    if len(names) != 2 or "OPENRFS" not in names or not any(
            re.fullmatch(r"[0-9A-F]{8}", name) for name in names):
        raise RuntimeError(f"Data root has visible paths: {names!r}")
    found = {needle.decode("ascii"): occurrences(raw, needle)
             for needle in OLD + NEW}
    if any(found[needle.decode("ascii")] for needle in NEW):
        raise RuntimeError("new Data names or content appeared in raw image")
    report = {"filesystem": args.filesystem, "root_names": names,
              "raw_occurrences": found}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"{args.filesystem} visible Data paths and raw bytes checked")


if __name__ == "__main__":
    main()
