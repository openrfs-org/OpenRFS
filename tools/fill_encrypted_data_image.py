#!/usr/bin/env python3
"""Leave too little free space for an encrypted Data rewrite."""

import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

import fat32_image


def repair_fat32_backup_fsinfo(image):
    with image.open("r+b") as volume:
        boot = volume.read(512)
        sector_bytes = struct.unpack_from("<H", boot, 11)[0]
        fsinfo = struct.unpack_from("<H", boot, 48)[0]
        backup_boot = struct.unpack_from("<H", boot, 50)[0]
        if sector_bytes != 512 or fsinfo == 0 or backup_boot == 0:
            raise RuntimeError("unexpected FAT32 FSInfo layout")
        volume.seek(fsinfo * sector_bytes)
        sector = volume.read(sector_bytes)
        volume.seek((backup_boot + fsinfo) * sector_bytes)
        volume.write(sector)


def debugfs(image, request, write=False):
    command = ["debugfs"]
    if write:
        command.append("-w")
    command += ["-R", request, str(image)]
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    if "error" in result.stderr.lower():
        raise RuntimeError(result.stderr)
    return result.stdout


def ext4_space(image):
    stats = debugfs(image, "stats")
    free = re.search(r"^Free blocks:\s+(\d+)$", stats, re.MULTILINE)
    block_size = re.search(r"^Block size:\s+(\d+)$", stats, re.MULTILINE)
    if free is None or block_size is None:
        raise RuntimeError("ext4 free-space fields missing")
    return int(free.group(1)), int(block_size.group(1))


def ext4_root(image):
    listing = debugfs(image, "ls -p /")
    roots = [part[5] for line in listing.splitlines()
             if (part := line.strip().split("/")) and len(part) >= 7 and
             re.fullmatch(r"[0-9A-F]{8}", part[5])]
    if len(roots) != 1:
        raise RuntimeError(f"expected one keyed Data root, got {roots!r}")
    return roots[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--filesystem", choices=("fat32", "ext4"), required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--remove", action="store_true")
    parser.add_argument("--leave-units", type=int, default=1)
    args = parser.parse_args()
    if args.leave_units < 1:
        parser.error("--leave-units must be positive")
    if args.source.resolve() == args.output.resolve():
        parser.error("source and output must differ")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.source, args.output)
    if args.filesystem == "fat32":
        raw = args.output.read_bytes()
        report = fat32_image.inspect_image(raw)
        roots = [entry["path"] for entry in report["files"]
                 if entry["directory"] and "/" not in entry["path"] and
                 re.fullmatch(r"[0-9A-F]{8}", entry["path"])]
        if len(roots) != 1:
            raise RuntimeError(f"expected one keyed Data root, got {roots!r}")
        cluster_bytes = report["sector_bytes"] * report["sectors_per_cluster"]
        if args.remove:
            fillers = [entry["path"] for entry in report["files"]
                       if re.fullmatch(rf"{roots[0]}/FILLERS/F[0-9]{{7}}\.BIN",
                                       entry["path"])]
            if not fillers:
                raise RuntimeError("FAT32 filler files are missing")
            for name in fillers:
                subprocess.run(("mdel", "-i", str(args.output),
                                "::" + name), check=True, capture_output=True)
            subprocess.run(("mrd", "-i", str(args.output),
                            f"::{roots[0]}/FILLERS"),
                           check=True, capture_output=True)
            repair_fat32_backup_fsinfo(args.output)
            fat32_image.inspect_image(args.output.read_bytes())
            print("fat32 filler removed")
            return
        subprocess.run(("mmd", "-i", str(args.output),
                        f"::{roots[0]}/FILLERS"),
                       check=True, capture_output=True)
        repair_fat32_backup_fsinfo(args.output)
        report = fat32_image.inspect_image(args.output.read_bytes())
        if report["free_clusters"] <= args.leave_units + 4:
            raise RuntimeError("FAT32 image is already too full")
        payload_bytes = (report["free_clusters"] - args.leave_units) * cluster_bytes
    else:
        root = ext4_root(args.output)
        directory = f"/{root}/FILLERS"
        if args.remove:
            listing = debugfs(args.output, f"ls -p {directory}")
            fillers = sorted(set(re.findall(r"F[0-9]{7}\.BIN", listing)))
            if not fillers:
                raise RuntimeError("ext4 filler files are missing")
            for name in fillers:
                debugfs(args.output, f"rm {directory}/{name}", write=True)
            debugfs(args.output, f"rmdir {directory}", write=True)
            print("ext4 filler removed")
            return
        debugfs(args.output, f"mkdir {directory}", write=True)
        free, block_size = ext4_space(args.output)
        if free <= args.leave_units + 32:
            raise RuntimeError("ext4 image is already too full")
    with tempfile.NamedTemporaryFile(prefix="openrfs-full-", delete=False) as tmp:
        temporary = Path(tmp.name)
    try:
        if args.filesystem == "ext4":
            block = b"\x5C" * 1048576
            with temporary.open("wb") as payload:
                left = 8 * 1024 * 1024
                while left:
                    amount = min(left, len(block))
                    payload.write(block[:amount])
                    left -= amount
        if args.filesystem == "fat32":
            left = payload_bytes
            index = 1
            while left:
                amount = min(left, 8 * 1024 * 1024)
                os.truncate(temporary, amount)
                destination = f"::{roots[0]}/FILLERS/F{index:07d}.BIN"
                subprocess.run(("mcopy", "-i", str(args.output),
                                str(temporary), destination),
                               check=True, capture_output=True)
                left -= amount
                index += 1
            repair_fat32_backup_fsinfo(args.output)
            remaining = fat32_image.inspect_image(args.output.read_bytes())[
                "free_clusters"]
            if remaining > args.leave_units:
                raise RuntimeError(f"FAT32 retained {remaining} free clusters")
        else:
            index = 1
            while True:
                remaining, block_size = ext4_space(args.output)
                available = remaining - args.leave_units - 2
                if available <= 0:
                    break
                amount = min(available * block_size, 8 * 1024 * 1024)
                os.truncate(temporary, amount)
                debugfs(args.output,
                        f"write {temporary} {directory}/F{index:07d}.BIN",
                        write=True)
                index += 1
            remaining, _ = ext4_space(args.output)
            if remaining > args.leave_units + 2:
                raise RuntimeError(f"ext4 retained {remaining} free blocks")
    finally:
        temporary.unlink(missing_ok=True)
    print(f"{args.filesystem} filled; {remaining} allocation units free")


if __name__ == "__main__":
    main()
