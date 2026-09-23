#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise QEMU CPU entropy present and absent paths on one OpenRFS image."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


def run_case(qemu: str, iso: pathlib.Path, output: pathlib.Path,
             cpu: str, marker: str, forbidden: str | None) -> None:
    command = [
        qemu, "-machine", "q35,accel=tcg", "-cpu", cpu,
        "-m", "128M", "-smp", "1", "-cdrom", str(iso),
        "-display", "none", "-monitor", "none", "-serial", "stdio",
        "-nic", "none", "-netdev", "user,id=n0",
        "-device", (
            "virtio-net-pci,id=virtio-net0,netdev=n0,"
            "mac=52:54:00:12:34:56,disable-legacy=on,mrg_rxbuf=off"
        ),
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
        "-no-reboot",
    ]
    try:
        completed = subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=120, check=False,
        )
    except subprocess.TimeoutExpired as error:
        output.write_bytes(error.stdout or b"")
        raise RuntimeError(f"{cpu}: guest timed out") from error
    output.write_bytes(completed.stdout)
    log = completed.stdout.decode("utf-8", errors="replace")
    if completed.returncode != 33:
        raise RuntimeError(
            f"{cpu}: guest exit {completed.returncode}, expected 33"
        )
    if "ST BEGIN normal" not in log or "ST PASS normal" not in log:
        raise RuntimeError(f"{cpu}: guest did not complete normal scenario")
    if marker not in log:
        raise RuntimeError(f"{cpu}: missing expected marker: {marker}")
    if forbidden and forbidden in log:
        raise RuntimeError(f"{cpu}: forbidden marker appeared: {forbidden}")
    print(f"{cpu}: expected guest exit 33, marker observed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    args = parser.parse_args()
    iso = args.iso.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=True)
    try:
        run_case(
            args.qemu, iso, args.output / "available.log", "max",
            "OpenRFS: virtio-net0 initialized", None,
        )
        run_case(
            args.qemu, iso, args.output / "rdrand-only.log", "max,-rdseed",
            "OpenRFS: virtio-net0 initialized", None,
        )
        run_case(
            args.qemu, iso, args.output / "absent.log",
            "max,-rdrand,-rdseed",
            "OpenRFS: networking refused: entropy unavailable",
            "OpenRFS: virtio-net0 initialized",
        )
    except RuntimeError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
