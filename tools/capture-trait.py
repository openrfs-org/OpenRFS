#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture the current Trait OS desktop applications from a live QEMU guest.

Every frame comes from QEMU's emulated display. Application launches travel
through the guest keyboard path, and the attached FAT32 data image is retained
for filesystem inspection with the capture artifact.
"""

import argparse
import json
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

import fat32_image
import importlib.util

_PROOF_PATH = Path(__file__).with_name("capture-trait-proof.py")
_PROOF_SPEC = importlib.util.spec_from_file_location("capture_trait_proof", _PROOF_PATH)
if _PROOF_SPEC is None or _PROOF_SPEC.loader is None:
    raise RuntimeError(f"cannot load capture helpers from {_PROOF_PATH}")
_PROOF = importlib.util.module_from_spec(_PROOF_SPEC)
_PROOF_SPEC.loader.exec_module(_PROOF)
PROOF_LINE = _PROOF.PROOF_LINE
Qmp = _PROOF.Qmp
capture = _PROOF.capture
free_port = _PROOF.free_port
storage_arguments = _PROOF.storage_arguments
wait_serial = _PROOF.wait_serial
wait_serial_after = _PROOF.wait_serial_after

PROMPT = b"trait> "
RUNTIME_FAILURE = b"runtime disabled"


def press(qmp, key, delay=0.30):
    qmp.hmp(f"sendkey {key}")
    time.sleep(delay)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--accel", choices=("tcg", "whpx"), default="tcg")
    parser.add_argument("--ffmpeg", default="ffmpeg", help=argparse.SUPPRESS)
    parser.add_argument("--iso", required=True)
    parser.add_argument("--system", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    durable_data = output / "trait-data.raw"
    shutil.copyfile(Path(args.data).resolve(), durable_data)
    serial = output / "trait-serial.log"
    if serial.exists():
        serial.unlink()

    port = free_port()
    command = [
        args.qemu, "-machine", f"accel={args.accel}", "-m", "128M",
        "-smp", "1", "-boot", "order=d", "-cdrom",
        str(Path(args.iso).resolve()), "-display", "none",
        *storage_arguments(None, args.system, durable_data),
        "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
        "-serial", f"file:{serial}", "-no-reboot",
    ]

    with tempfile.TemporaryDirectory(prefix="trait-desktop-capture-"):
        process = subprocess.Popen(
            command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        qmp = None
        try:
            qmp = Qmp(port)
            wait_serial(serial, PROOF_LINE, timeout=90.0)
            wait_serial_after(serial, PROOF_LINE, PROMPT, timeout=90.0)
            time.sleep(0.35)

            # Files is opened by the desktop bootstrap and keyboard focus starts
            # on its launcher. Each close/Tab/Enter sequence selects the next
            # product application without depending on host pointer geometry.
            capture(qmp, output, "trait-files")
            press(qmp, "esc")
            capture(qmp, output, "trait-desktop")

            applications = (
                ("trait-terminal", 1),
                ("trait-task-manager", 1),
                ("trait-packages", 1),
                ("trait-settings", 1),
            )
            for name, tabs in applications:
                for _ in range(tabs):
                    press(qmp, "tab", 0.15)
                press(qmp, "ret", 0.40)
                capture(qmp, output, name)
                press(qmp, "esc", 0.25)
        finally:
            if qmp is not None:
                try:
                    qmp.execute("quit")
                except (OSError, RuntimeError):
                    pass
                qmp.close()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    transcript = serial.read_bytes() if serial.exists() else b""
    if PROOF_LINE not in transcript or PROMPT not in transcript or \
            RUNTIME_FAILURE in transcript:
        tail = transcript[-4096:].decode("utf-8", errors="replace")
        raise RuntimeError("desktop capture omitted readiness evidence\n" + tail)

    report = fat32_image.inspect_image(durable_data.read_bytes())
    if (not bool(report["fat_copies_match"]) or int(report["cycles"]) != 0 or
            int(report["cross_links"]) != 0 or
            int(report["leaked_clusters"]) != 0):
        raise RuntimeError("desktop capture left an inconsistent FAT32 image")
    (output / "report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(output)


if __name__ == "__main__":
    main()
