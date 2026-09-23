#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture three deterministic OpenRFS frames from a live QEMU guest.

QMP supplies both the screenshot command and narrowly scoped HMP input. The
images therefore come from the emulated display device; no UI is recreated on
the host. Only Python's standard library is required.
"""

import argparse
import json
import os
import shutil
import socket
import struct
import subprocess
import time
import zlib
from pathlib import Path

import fat32_image


PROOF_LINE = b"OpenRFS: BT11 Boot Ledger installed proof passed"
TERMINAL_COMMAND = "echo openrfs"
TERMINAL_RESULT = b"echo openrfs\nopenrfs\nopenrfs$ "
GFETCH_RESULT = b"kernel      OpenRFS 2.4.0 / x86_64"
PROMPT = b"openrfs$ "
NEW_PASSWORD_PROMPT = b"New password (8-64 characters): "
CONFIRM_PASSWORD_PROMPT = b"Confirm password: "
USERNAME_PROMPT = b"Username: "
PASSWORD_PROMPT = b"Password: "
ACCOUNT_CREATED = b"OpenRFS user created. Run 'starty' to enter the desktop."
DESKTOP_STARTED = b"OpenRFS: authenticated desktop started"
CAPTURE_USERNAME = "openrfs"
CAPTURE_PASSWORD = "openrfspass"
RUNTIME_FAILURE = b"runtime disabled"


def png_chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(
        ">I", zlib.crc32(kind + body) & 0xFFFFFFFF
    )


def ppm_to_png(source, destination):
    data = Path(source).read_bytes()
    tokens = []
    position = 0
    while len(tokens) < 4:
        while position < len(data) and data[position] in b" \t\r\n":
            position += 1
        if position < len(data) and data[position] == ord("#"):
            position = data.find(b"\n", position) + 1
            continue
        end = position
        while end < len(data) and data[end] not in b" \t\r\n":
            end += 1
        tokens.append(data[position:end])
        position = end
    if tokens[0] != b"P6" or tokens[3] != b"255":
        raise RuntimeError("QEMU screendump is not an 8-bit binary PPM")
    width, height = int(tokens[1]), int(tokens[2])
    if position >= len(data) or data[position] not in b" \t\r\n":
        raise RuntimeError("QEMU screendump header has no pixel separator")
    separator = data[position]
    position += 1
    if separator == ord("\r") and position < len(data) and data[position] == ord("\n"):
        position += 1
    pixels = data[position:]
    if len(pixels) != width * height * 3:
        raise RuntimeError(
            f"QEMU screendump pixel body has {len(pixels)} bytes; "
            f"expected {width * height * 3}"
        )
    rows = b"".join(
        b"\x00" + pixels[y * width * 3:(y + 1) * width * 3]
        for y in range(height)
    )
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += png_chunk(b"IDAT", zlib.compress(rows, 9))
    png += png_chunk(b"IEND", b"")
    Path(destination).write_bytes(png)


class Qmp:
    def __init__(self, port):
        deadline = time.monotonic() + 10.0
        while True:
            try:
                self.socket = socket.create_connection(("127.0.0.1", port), 0.5)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise RuntimeError("QMP did not accept a connection")
                time.sleep(0.05)
        self.file = self.socket.makefile("rwb", buffering=0)
        self._read_message()
        self.execute("qmp_capabilities")

    def _read_message(self):
        while True:
            line = self.file.readline()
            if not line:
                raise RuntimeError("QMP disconnected")
            message = json.loads(line)
            if "event" not in message:
                return message

    def execute(self, command, arguments=None):
        request = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.file.write(json.dumps(request).encode("ascii") + b"\r\n")
        response = self._read_message()
        if "error" in response:
            raise RuntimeError(f"QMP {command} failed: {response['error']}")
        return response.get("return")

    def hmp(self, command):
        return self.execute("human-monitor-command", {"command-line": command})

    def close(self):
        self.file.close()
        self.socket.close()


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def wait_serial(path, marker, timeout=35.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists() and marker in path.read_bytes():
            return
        time.sleep(0.05)
    transcript = path.read_bytes() if path.exists() else b""
    tail = transcript[-8192:].decode("utf-8", errors="replace")
    raise RuntimeError(
        f"serial transcript omitted {marker.decode('ascii')}\n"
        f"--- serial transcript tail ({len(transcript)} bytes total) ---\n"
        f"{tail}\n--- end serial transcript tail ---"
    )


def wait_serial_after(path, anchor, marker, timeout=35.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            transcript = path.read_bytes()
            position = transcript.find(anchor)
            if position >= 0 and marker in transcript[position + len(anchor):]:
                return
        time.sleep(0.05)
    transcript = path.read_bytes() if path.exists() else b""
    tail = transcript[-8192:].decode("utf-8", errors="replace")
    raise RuntimeError(
        f"serial transcript omitted {marker.decode('ascii')} after "
        f"{anchor.decode('ascii')}\n"
        f"--- serial transcript tail ({len(transcript)} bytes total) ---\n"
        f"{tail}\n--- end serial transcript tail ---"
    )


def capture(qmp, directory, stem):
    ppm = directory / f"{stem}.ppm"
    png = directory / f"{stem}.png"
    qmp.execute("screendump", {
        "filename": ppm.resolve().as_posix(), "format": "ppm"
    })
    ppm_to_png(ppm, png)
    ppm.unlink()
    return png


def send_text(qmp, text, delay=0.04):
    for key in text:
        qmp.hmp(f"sendkey {'spc' if key == ' ' else key}")
        time.sleep(delay)

def press(qmp, key, delay=0.30):
    qmp.hmp(f"sendkey {key}")
    time.sleep(delay)


def start_authenticated_desktop(qmp, serial):
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


def storage_arguments(userspace, system, data):
    arguments = []
    if system is not None and data is not None:
        arguments.extend([
            "-blockdev",
            f"driver=file,filename={Path(system).resolve()},node-name=system-file,read-only=on,auto-read-only=off",
            "-blockdev",
            "driver=raw,file=system-file,node-name=system-raw,read-only=on",
            "-device",
            "nvme,serial=openrfs-system-fat32,drive=system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1",
            "-blockdev",
            f"driver=file,filename={Path(data).resolve()},node-name=data-file,read-only=off,auto-read-only=off",
            "-blockdev",
            "driver=raw,file=data-file,node-name=data-raw,read-only=off",
            "-device",
            "nvme,serial=openrfs-data-fat32,drive=data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1",
        ])
    if userspace is not None:
        arguments.extend([
            "-blockdev",
            f"driver=file,filename={Path(userspace).resolve()},node-name=userland-file,read-only=on,auto-read-only=off",
            "-blockdev",
            "driver=raw,file=userland-file,node-name=userland-raw,read-only=on",
            "-device",
            "nvme,serial=openrfs-userland,drive=userland-raw,logical_block_size=4096,physical_block_size=4096,max_ioqpairs=1,msix_qsize=1",
        ])
    return arguments


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--cpu", default="max")
    parser.add_argument("--iso", required=True)
    parser.add_argument("--userspace")
    parser.add_argument("--system")
    parser.add_argument("--data")
    parser.add_argument("--output", required=True)


    args = parser.parse_args()
    if args.userspace is None and args.system is None:
        parser.error("provide --userspace or the --system/--data pair")
    if (args.system is None) != (args.data is None):
        parser.error("--system and --data must be provided together")

    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    durable_data = None
    if args.data is not None:
        durable_data = output / "openrfs-proof-data.raw"
        shutil.copyfile(Path(args.data).resolve(), durable_data)
    serial = output / "capture-serial.log"
    if serial.exists():
        serial.unlink()
    port = free_port()
    command = [
        args.qemu, "-machine", "accel=tcg", "-cpu", args.cpu,
        "-m", "128M", "-smp", "1",
        "-boot", "order=d", "-cdrom", str(Path(args.iso).resolve()),
        "-display", "none",
        *storage_arguments(args.userspace, args.system, durable_data),
        "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
        "-serial", f"file:{serial}", "-no-reboot"
    ]
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    qmp = None
    try:
        qmp = Qmp(port)
        wait_serial(serial, PROOF_LINE, timeout=90.0)
        wait_serial_after(serial, PROOF_LINE, PROMPT, timeout=90.0)
        if durable_data is not None:
            start_authenticated_desktop(qmp, serial)
        time.sleep(0.25)
        # starty opens the minimal desktop with its terminal attached to the
        # production shell. Capture the initial guest frame, then exercise
        # gfetch and a second command through the same PS/2 keyboard path.
        clean = capture(qmp, output, "openrfs-proof")
        send_text(qmp, "gfetch")
        qmp.hmp("sendkey ret")
        wait_serial_after(serial, DESKTOP_STARTED, GFETCH_RESULT)
        time.sleep(0.20)
        focus = capture(qmp, output, "openrfs-proof-focus")

        send_text(qmp, TERMINAL_COMMAND)
        qmp.hmp("sendkey ret")
        wait_serial_after(serial, DESKTOP_STARTED, TERMINAL_RESULT)
        time.sleep(0.20)
        terminal = capture(qmp, output, "openrfs-proof-terminal")
        print(clean)
        print(focus)
        print(terminal)
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
    if (PROOF_LINE not in transcript or ACCOUNT_CREATED not in transcript or
            DESKTOP_STARTED not in transcript or GFETCH_RESULT not in transcript or
            TERMINAL_RESULT not in transcript or
            CAPTURE_PASSWORD.encode("ascii") in transcript or
            RUNTIME_FAILURE in transcript):
        tail = transcript[-4096:].decode("utf-8", errors="replace")
        raise RuntimeError("proof capture omitted readiness evidence\n" + tail)
    if durable_data is not None:
        report = fat32_image.inspect_image(durable_data.read_bytes())
        login = [
            item for item in report["files"]
            if item["path"] == "OPENRFS/LOGIN.DAT" and not item["directory"]
        ]
        if (not bool(report["fat_copies_match"]) or int(report["cycles"]) != 0 or
                int(report["cross_links"]) != 0 or
                int(report["leaked_clusters"]) != 0 or len(login) != 1 or
                int(login[0]["size"]) != 124):
            raise RuntimeError("proof capture left an inconsistent account volume")
        (output / "report.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )


if __name__ == "__main__":
    main()
