#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prove the merged driver NIC obeys the kernel entropy boundary in QEMU."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

RUNNER = Path(__file__).with_name("run_driver_tests.py")
MAX_LOG_BYTES = 1024 * 1024


def case(kernel: Path, output: Path, qemu: str, grub: str,
         cpu: str, expect_refusal: bool) -> None:
    result = subprocess.run(
        [sys.executable, str(RUNNER), "--kernel", str(kernel),
         "--output", str(output), "--qemu", qemu,
         "--grub-mkrescue", grub, "--scenario", "net-e1000",
         "--cpu", cpu],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=120, check=False,
    )
    log = output / "net-e1000" / "serial.log"
    if not log.is_file() or log.stat().st_size > MAX_LOG_BYTES:
        raise RuntimeError(f"{cpu}: missing or oversized guest log")
    guest = log.read_text(errors="replace")
    if expect_refusal:
        if (result.returncode != 1 or
                "exit status 255 (expected 17)" not in result.stdout or
                "OpenRFS: networking refused: entropy unavailable" not in guest or
                "ST FAIL drivers: no upstream network interface is active" not in guest or
                "ST PASS drivers" in guest):
            raise RuntimeError(f"{cpu}: missing exact entropy refusal")
        print(f"{cpu}: guest refusal and exit 255 observed")
    else:
        if (result.returncode != 0 or
                "PASS net-e1000:" not in result.stdout or
                guest.count("ST PASS drivers") != 1 or
                "OpenRFS: networking refused: entropy unavailable" in guest):
            raise RuntimeError(f"{cpu}: restored driver network pass absent")
        print(f"{cpu}: driver network pass restored")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--grub-mkrescue", default="grub-mkrescue")
    args = parser.parse_args()
    try:
        case(args.kernel, args.output / "absent", args.qemu,
             args.grub_mkrescue, "max,-rdrand,-rdseed", True)
        case(args.kernel, args.output / "available", args.qemu,
             args.grub_mkrescue, "max", False)
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
