#!/usr/bin/env python3
"""Put a known plaintext file on a FAT32 or ext4 Data test image."""

import argparse
from pathlib import Path
import shutil
import struct
import subprocess


SECRET = b"OPENRFS_PLAINTEXT_SENTINEL_9466\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--filesystem", choices=("fat32", "ext4"), required=True)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--xattr", action="store_true")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.base, args.output)
    payload = args.output.parent / "secret.txt"
    payload.write_bytes(SECRET)
    if args.filesystem == "fat32":
        if args.xattr:
            parser.error("FAT32 has no extended attributes")
        subprocess.run(("mcopy", "-i", str(args.output), str(payload),
                        "::/SECRET.TXT"), check=True)
        with args.output.open("r+b") as image:
            boot = image.read(512)
            sector_bytes = struct.unpack_from("<H", boot, 11)[0]
            fsinfo = struct.unpack_from("<H", boot, 48)[0]
            backup_boot = struct.unpack_from("<H", boot, 50)[0]
            if sector_bytes != 512 or fsinfo == 0 or backup_boot == 0:
                raise RuntimeError("unexpected FAT32 FSInfo layout")
            image.seek(fsinfo * sector_bytes)
            sector = image.read(sector_bytes)
            image.seek((backup_boot + fsinfo) * sector_bytes)
            image.write(sector)
    else:
        subprocess.run(("debugfs", "-w", "-R",
                        f"write {payload} /SECRET.TXT", str(args.output)),
                       check=True, capture_output=True, text=True)
        result = subprocess.run(("debugfs", "-R", "stat /SECRET.TXT",
                                 str(args.output)), check=True,
                                capture_output=True, text=True)
        if "Type: regular" not in result.stdout:
            raise RuntimeError("ext4 plaintext fixture was not written")
        if args.xattr:
            subprocess.run(("debugfs", "-w", "-R",
                            "ea_set /SECRET.TXT user.openrfs marker",
                            str(args.output)), check=True,
                           capture_output=True, text=True)
            result = subprocess.run(("debugfs", "-R",
                                     "ea_get /SECRET.TXT user.openrfs",
                                     str(args.output)), check=True,
                                    capture_output=True, text=True)
            if "marker" not in result.stdout:
                raise RuntimeError("ext4 extended attribute was not written")


if __name__ == "__main__":
    main()
