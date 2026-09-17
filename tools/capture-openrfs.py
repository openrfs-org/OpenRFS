#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture the current OpenRFS desktop applications from a live QEMU guest.

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

_PROOF_PATH = Path(__file__).with_name("capture-openrfs-proof.py")
_PROOF_SPEC = importlib.util.spec_from_file_location("capture_openrfs_proof", _PROOF_PATH)
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
send_text = _PROOF.send_text

PROMPT = b"openrfs$ "
NEW_PASSWORD_PROMPT = b"New password (8-64 characters): "
CONFIRM_PASSWORD_PROMPT = b"Confirm password: "
USERNAME_PROMPT = b"Username: "
PASSWORD_PROMPT = b"Password: "
ACCOUNT_CREATED = b"OpenRFS user created. Run 'starty' to enter the desktop."
DESKTOP_STARTED = b"OpenRFS: authenticated desktop started"
RUNTIME_FAILURE = b"runtime disabled"
CAPTURE_USERNAME = "openrfs"
CAPTURE_PASSWORD = "openrfspass"


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
    durable_data = output / "openrfs-data.raw"
    shutil.copyfile(Path(args.data).resolve(), durable_data)
    serial = output / "openrfs-serial.log"
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

    with tempfile.TemporaryDirectory(prefix="openrfs-desktop-capture-"):
        process = subprocess.Popen(
            command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        qmp = None
        try:
            qmp = Qmp(port)
            wait_serial(serial, PROOF_LINE, timeout=90.0)
            wait_serial_after(serial, PROOF_LINE, PROMPT, timeout=90.0)
            capture(qmp, output, "openrfs-console")

            # Exercise the installed first-user and starty path through the
            # guest keyboard. The password is deliberately public test data,
            # and the serial audit below proves that password entry was hidden.
            send_text(qmp, f"useradd {CAPTURE_USERNAME}")
            press(qmp, "ret", 0.10)
            wait_serial_after(serial, PROMPT, NEW_PASSWORD_PROMPT, timeout=30.0)
            send_text(qmp, CAPTURE_PASSWORD)
            press(qmp, "ret", 0.10)
            wait_serial(serial, CONFIRM_PASSWORD_PROMPT, timeout=30.0)
            send_text(qmp, CAPTURE_PASSWORD)
            press(qmp, "ret", 0.10)
            wait_serial(serial, ACCOUNT_CREATED, timeout=90.0)
            wait_serial_after(serial, ACCOUNT_CREATED, PROMPT, timeout=30.0)
            send_text(qmp, "starty")
            press(qmp, "ret", 0.10)
            wait_serial(serial, USERNAME_PROMPT, timeout=30.0)
            send_text(qmp, CAPTURE_USERNAME)
            press(qmp, "ret", 0.10)
            wait_serial(serial, PASSWORD_PROMPT, timeout=30.0)
            send_text(qmp, CAPTURE_PASSWORD)
            press(qmp, "ret", 0.10)
            wait_serial(serial, DESKTOP_STARTED, timeout=90.0)
            time.sleep(0.35)

            # Files is opened by the desktop bootstrap and keyboard focus starts
            # on its launcher. Each close/Tab/Enter sequence selects the next
            # product application without depending on host pointer geometry.
            capture(qmp, output, "openrfs-files")
            press(qmp, "esc")
            capture(qmp, output, "openrfs-desktop")

            applications = (
                ("openrfs-terminal", 1),
                ("openrfs-task-manager", 1),
                ("openrfs-packages", 1),
                ("openrfs-settings", 1),
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
    if (PROOF_LINE not in transcript or PROMPT not in transcript or
            ACCOUNT_CREATED not in transcript or DESKTOP_STARTED not in transcript or
            CAPTURE_PASSWORD.encode("ascii") in transcript or
            RUNTIME_FAILURE in transcript):
        tail = transcript[-4096:].decode("utf-8", errors="replace")
        raise RuntimeError("desktop capture omitted readiness evidence\n" + tail)

    report = fat32_image.inspect_image(durable_data.read_bytes())
    login = [
        item for item in report["files"]
        if item["path"] == "OPENRFS/LOGIN.DAT" and not item["directory"]
    ]
    if (not bool(report["fat_copies_match"]) or int(report["cycles"]) != 0 or
            int(report["cross_links"]) != 0 or
            int(report["leaked_clusters"]) != 0 or len(login) != 1 or
            int(login[0]["size"]) != 124):
        raise RuntimeError("desktop capture left an inconsistent account volume")
    (output / "report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(output)


if __name__ == "__main__":
    main()
