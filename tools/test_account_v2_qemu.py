#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Boot retained account media and exercise production login refusals."""

import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
import time
from pathlib import Path

import fat32_image


def load_capture():
    source = Path(__file__).with_name("capture-openrfs-proof.py")
    spec = importlib.util.spec_from_file_location("capture_openrfs_proof", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def record_offset(image):
    report = fat32_image.inspect_image(image)
    records = [entry for entry in report["files"] if entry["path"] in
               ("OPENRFS/LOGIN.V2A", "OPENRFS/LOGIN.V2B")]
    if len(records) != 1 or records[0]["size"] != 224:
        raise RuntimeError("input needs exactly one 224-byte v2 credential")
    geometry = fat32_image.parse_geometry(image)
    return geometry.sector_offset(geometry.cluster_sector(
        int(records[0]["first_cluster"])))


def case_image(source, destination, case):
    shutil.copyfile(source, destination)
    if case in ("valid", "wrong-password", "locked-shell"):
        return
    image = bytearray(destination.read_bytes())
    offset = record_offset(image)
    if case == "malformed":
        image[offset + 7] = 1  # Reserved header byte.
    elif case == "forged-checksum":
        image[offset + 140] ^= 0x40  # Data-key ciphertext, outside verifier.
        image[offset + 188:offset + 220] = hashlib.sha256(
            image[offset:offset + 188]).digest()
    else:
        raise ValueError(case)
    fat32_image.inspect_image(image)  # Preserve filesystem geometry and FAT.
    destination.write_bytes(image)


def boot_case(capture, args, case, image, directory):
    serial = directory / f"{case}.serial.log"
    prior_image_hash = hashlib.sha256(image.read_bytes()).hexdigest() if (
        case == "locked-shell") else None
    port = capture.free_port()
    command = [args.qemu, "-machine", "accel=tcg", "-cpu", "max",
               "-m", "128M", "-smp", "1", "-boot", "order=d",
               "-cdrom", str(args.iso.resolve()), "-display", "none",
               *capture.storage_arguments(None, args.system if image else None,
                                          image),
               "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
               "-serial", f"file:{serial}", "-no-reboot"]
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    qmp = None
    try:
        qmp = capture.Qmp(port)
        capture.wait_serial(serial, capture.PROOF_LINE, timeout=90.0)
        capture.wait_serial_after(serial, capture.PROOF_LINE,
                                  capture.PROMPT, timeout=90.0)
        if case == "storage-unavailable":
            expected = b"account: the writable data volume is unavailable"
            observed = serial.read_bytes().count(expected)
            for command_text in ("native", "read secret"):
                capture.send_text(qmp, command_text)
                capture.press(qmp, "ret", 0.1)
                deadline = time.monotonic() + 30.0
                observed += 1
                while serial.read_bytes().count(expected) < observed:
                    if time.monotonic() >= deadline:
                        raise RuntimeError("unavailable storage admitted a command")
                    time.sleep(0.05)
        elif case == "locked-shell":
            capture.send_text(qmp, "read secret")
            capture.press(qmp, "ret", 0.1)
            expected = b"account: login required; run 'starty'"
            capture.wait_serial(serial, expected)
            capture.send_text(qmp, "touch unauth")
            capture.press(qmp, "ret", 0.1)
            deadline = time.monotonic() + 30.0
            while serial.read_bytes().count(expected) < 2:
                if time.monotonic() >= deadline:
                    raise RuntimeError("second locked shell command was not refused")
                time.sleep(0.05)
            capture.send_text(qmp, "write secret attacker")
            capture.press(qmp, "ret", 0.1)
            deadline = time.monotonic() + 30.0
            while serial.read_bytes().count(expected) < 3:
                if time.monotonic() >= deadline:
                    raise RuntimeError("content-bearing overwrite was not refused")
                time.sleep(0.05)
        else:
            capture.send_text(qmp, "starty")
            capture.press(qmp, "ret", 0.1)
        if case == "malformed":
            expected = b"account: the OpenRFS account record is corrupt"
            capture.wait_serial(serial, expected)
        elif case not in ("locked-shell", "storage-unavailable"):
            capture.wait_serial_after(serial, capture.PROMPT,
                                      capture.USERNAME_PROMPT)
            capture.send_text(qmp, capture.CAPTURE_USERNAME)
            capture.press(qmp, "ret", 0.1)
            capture.wait_serial(serial, capture.PASSWORD_PROMPT)
            password = "wrong-password" if case == "wrong-password" else (
                capture.ROTATED_PASSWORD if args.rotated else
                capture.CAPTURE_PASSWORD)
            capture.send_text(qmp, password)
            capture.press(qmp, "ret", 0.1)
            expected = {
                "valid": capture.DESKTOP_STARTED,
                "valid-reboot": capture.DESKTOP_STARTED,
                "wrong-password": b"account: invalid username or password",
                "forged-checksum": b"account: the OpenRFS account record is corrupt",
            }[case]
            capture.wait_serial(serial, expected, timeout=90.0)
            if case == "valid":
                command = "write secret milestonesecret"
                capture.send_text(qmp, command)
                capture.press(qmp, "ret", 0.1)
                capture.wait_serial_after(serial, command.encode(),
                                          capture.PROMPT, timeout=120.0)
            elif case == "valid-reboot":
                command = "read secret"
                capture.send_text(qmp, command)
                capture.press(qmp, "ret", 0.1)
                capture.wait_serial_after(serial, command.encode(),
                                          b"milestonesecret", timeout=120.0)
                capture.wait_serial_after(serial, command.encode(),
                                          capture.PROMPT, timeout=120.0)
        time.sleep(0.25)
        transcript = serial.read_bytes()
        if case not in ("valid", "valid-reboot") and capture.DESKTOP_STARTED in transcript:
            raise RuntimeError(f"{case} reached the desktop")
        if case in ("valid", "valid-reboot") and capture.DESKTOP_STARTED not in transcript:
            raise RuntimeError("valid record did not reach the desktop")
        if case == "locked-shell":
            if hashlib.sha256(image.read_bytes()).hexdigest() != prior_image_hash:
                raise RuntimeError("locked shell changed the Data image")
            if b"milestonesecret" in transcript:
                raise RuntimeError("locked shell exposed the stored Data content")
        if case in ("valid", "valid-reboot"):
            raw = image.read_bytes()
            report = fat32_image.inspect_image(raw)
            if any(entry["path"] == "SECRET" for entry in report["files"]):
                raise RuntimeError("Data filename appeared in the raw image")
            if b"milestonesecret" in raw:
                raise RuntimeError("Data content appeared in the raw image")
        if (capture.CAPTURE_PASSWORD.encode() in transcript or
                capture.ROTATED_PASSWORD.encode() in transcript):
            raise RuntimeError("serial output leaked a password")
        return {"case": case, "serial_sha256": hashlib.sha256(transcript).hexdigest(),
                "image_sha256": hashlib.sha256(image.read_bytes()).hexdigest()
                    if image is not None else None,
                "expected": expected.decode(),
                "observed": True}
    finally:
        if qmp is not None:
            try:
                qmp.execute("quit")
            except (OSError, RuntimeError):
                pass
            qmp.close()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--system", required=True, type=Path)
    parser.add_argument("--data", required=True, type=Path,
                        help="retained Data image with a single v2 credential")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--rotated", action="store_true")
    parser.add_argument("--storage-unavailable-only", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    capture = load_capture()
    results = []
    source = args.data
    cases = ("storage-unavailable",) if args.storage_unavailable_only else (
        "valid", "valid-reboot", "locked-shell", "wrong-password", "malformed",
        "forged-checksum", "storage-unavailable")
    for case in cases:
        image = None if case == "storage-unavailable" else (
            source if case == "valid-reboot" else args.output / f"{case}.raw")
        if image is not None and case != "valid-reboot":
            case_image(source, image, case)
        results.append(boot_case(capture, args, case, image, args.output))
        if case == "valid":
            source = image
        print(f"account v2 QEMU {case}: expected refusal/acceptance observed", flush=True)
    (args.output / "report.json").write_text(
        json.dumps(results, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
