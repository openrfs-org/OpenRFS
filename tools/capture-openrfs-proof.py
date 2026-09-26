#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture three deterministic OpenRFS frames from a live QEMU guest.

QMP supplies both the screenshot command and narrowly scoped HMP input. The
images therefore come from the emulated display device; no UI is recreated on
the host. Only Python's standard library is required.
"""

import argparse
import hashlib
import re
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
import ext4_image


PROOF_LINE = b"OpenRFS: BT11 Boot Ledger installed proof passed"
TERMINAL_COMMAND = "echo openrfs"
TERMINAL_RESULT = b"echo openrfs\nopenrfs\nopenrfs$ "
GFETCH_RESULT = b"kernel      OpenRFS 2.5 beta / x86_64"
PROMPT = b"openrfs$ "
NEW_PASSWORD_PROMPT = b"New password (8-64 characters): "
CONFIRM_PASSWORD_PROMPT = b"Confirm password: "
USERNAME_PROMPT = b"Username: "
PASSWORD_PROMPT = b"Password: "
ACCOUNT_CREATED = b"OpenRFS user created. Run 'starty' to enter the desktop."
DESKTOP_STARTED = b"OpenRFS: authenticated desktop started"
CAPTURE_USERNAME = "openrfs"
CAPTURE_PASSWORD = "openrfspass"
ROTATED_PASSWORD = "openrfsnewpass"
PASSWORD_CHANGED = b"OpenRFS password changed."
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
    return width, height, pixels


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


def encrypted_data_shell_check(qmp, serial):
    steps = (
        ("read SECRET.TXT", b"OPENRFS_PLAINTEXT_SENTINEL_9466"),
        ("write SECRET.TXT amber", None),
        ("read SECRET.TXT", b"amber\n"),
        ("append SECRET.TXT blue", None),
        ("read SECRET.TXT", b"amber\nblue\n"),
        ("writeat SECRET.TXT 0 gold", None),
        ("truncate SECRET.TXT 5", None),
        ("read SECRET.TXT", b"gold\n"),
        ("mv SECRET.TXT HIDDEN.TXT", None),
        ("read HIDDEN.TXT", b"gold\n"),
        ("mkdir VAULT", None),
        ("write VAULT/NOTE.TXT nested", None),
        ("mv VAULT CLOSED", None),
        ("read CLOSED/NOTE.TXT", b"nested\n"),
        ("rm CLOSED/NOTE.TXT", None),
        ("rm CLOSED", None),
        ("rm HIDDEN.TXT", None),
    )
    for command, expected in steps:
        start = len(serial.read_bytes())
        send_text(qmp, command)
        press(qmp, "ret", 0.10)
        command_end = command.encode("ascii") + b"\n"
        deadline = time.monotonic() + 40.0
        while time.monotonic() < deadline:
            output = serial.read_bytes()[start:]
            echo_end = output.find(command_end)
            if echo_end >= 0:
                response = output[echo_end + len(command_end):]
                prompt = response.find(PROMPT)
                if prompt >= 0:
                    response = response[:prompt]
                    if expected is not None and expected not in response:
                        raise RuntimeError(f"{command!r} returned {response!r}")
                    if b": " in response:
                        raise RuntimeError(f"{command!r} failed: {response!r}")
                    break
            time.sleep(0.05)
        else:
            raise RuntimeError(f"{command!r} did not return to the shell")


def rotate_encrypted_data_password(qmp, serial):
    send_text(qmp, "passwd")
    press(qmp, "ret", 0.10)
    wait_serial_after(serial, TERMINAL_RESULT, USERNAME_PROMPT)
    send_text(qmp, CAPTURE_USERNAME)
    press(qmp, "ret", 0.10)
    current_prompt = b"Current password: "
    wait_serial_after(serial, TERMINAL_RESULT, current_prompt)
    send_text(qmp, CAPTURE_PASSWORD)
    press(qmp, "ret", 0.10)
    wait_serial_after(serial, current_prompt, NEW_PASSWORD_PROMPT)
    send_text(qmp, ROTATED_PASSWORD)
    press(qmp, "ret", 0.10)
    wait_serial_after(serial, current_prompt, b"Confirm new password: ")
    send_text(qmp, ROTATED_PASSWORD)
    press(qmp, "ret", 0.10)
    wait_serial_after(serial, current_prompt, PASSWORD_CHANGED, timeout=90.0)


def native_encrypted_data_check(qmp, serial):
    start = len(serial.read_bytes())
    send_text(qmp, "native NATIVET.MAN")
    press(qmp, "ret", 0.10)
    deadline = time.monotonic() + 900.0
    while time.monotonic() < deadline:
        output = serial.read_bytes()[start:]
        if b"openrfs$ " in output:
            if b"OPENRFS NATIVE PASS" not in output or b"released=yes" not in output:
                raise RuntimeError(f"native Data probe failed: {output!r}")
            return
        time.sleep(0.05)
    raise RuntimeError("native Data probe did not return to the shell")


def upload_encrypted_data_check(qmp, serial):
    start = len(serial.read_bytes())
    send_text(qmp, "native ENCUPL.MAN")
    press(qmp, "ret", 0.10)
    deadline = time.monotonic() + 300.0
    while time.monotonic() < deadline:
        output = serial.read_bytes()[start:]
        if b"openrfs$ " in output:
            if (b"OPENRFS NATIVE DATA PASS" not in output or
                    b"OPENRFS UPLOAD PASS" not in output or
                    b"released=yes" not in output):
                raise RuntimeError(f"package upload probe failed: {output!r}")
            return
        time.sleep(0.05)
    raise RuntimeError("package upload probe did not return to the shell")


def disk_full_encrypted_data_check(qmp, serial):
    start = len(serial.read_bytes())
    send_text(qmp, "append SECRET.TXT extra")
    press(qmp, "ret", 0.10)
    wait_serial_after(serial, b"append SECRET.TXT extra", PROMPT, timeout=90.0)
    output = serial.read_bytes()[start:]
    if b"append: " not in output:
        raise RuntimeError(f"disk-full append was not refused: {output!r}")
    start = len(serial.read_bytes())
    send_text(qmp, "read SECRET.TXT")
    press(qmp, "ret", 0.10)
    wait_serial_after(serial, b"read SECRET.TXT", PROMPT, timeout=30.0)
    output = serial.read_bytes()[start:]
    if b"account: login required" not in output:
        raise RuntimeError(f"Data session survived storage failure: {output!r}")


def capture(qmp, directory, stem, frame=None):
    ppm = directory / f"{stem}.ppm"
    png = directory / f"{stem}.png"
    qmp.execute("screendump", {
        "filename": ppm.resolve().as_posix(), "format": "ppm"
    })
    pixels = ppm_to_png(ppm, png)
    if frame is not None:
        frame.append(pixels)
    ppm.unlink()
    return png


def verify_wvrm_files(root_frame, files_frame, data_frame):
    if any(frame[:2] != (1024, 768) for frame in
           (root_frame, files_frame, data_frame)):
        raise RuntimeError("WVRM Files proof requires the measured 1024x768 display")
    def rgb(frame, x, y):
        at = (y * frame[0] + x) * 3
        return frame[2][at:at + 3]
    if rgb(root_frame, 400, 300) == rgb(files_frame, 400, 300) or (
            rgb(files_frame, 400, 300) != b"\xff\xff\xff"):
        raise RuntimeError("WVRM Files window did not replace the terminal pixels")
    changed = 0
    for y in range(226, 350):
        for x in range(250, 600):
            if rgb(files_frame, x, y) != rgb(data_frame, x, y):
                changed += 1
    if changed < 100 or rgb(data_frame, 400, 400) != b"\xff\xff\xff":
        raise RuntimeError("WVRM Data directory did not show VFS entries")


def verify_wvrm_revoked(data_frame, revoked_frame):
    if data_frame[:2] != revoked_frame[:2]:
        raise RuntimeError("WVRM revocation capture changed display dimensions")
    for x, y in ((400, 400), (500, 500)):
        at = (y * data_frame[0] + x) * 3
        if (data_frame[2][at:at + 3] != b"\xff\xff\xff" or
                revoked_frame[2][at:at + 3] == b"\xff\xff\xff"):
            raise RuntimeError("WVRM Files remained visible after account deletion")


def send_text(qmp, text, delay=0.04):
    for key in text:
        sent = f"shift-{key.lower()}" if key.isupper() else (
            {" ": "spc", ".": "dot", "/": "slash"}.get(key, key))
        qmp.hmp(f"sendkey {sent}")
        time.sleep(delay)

def press(qmp, key, delay=0.30):
    qmp.hmp(f"sendkey {key}")
    time.sleep(delay)


def start_authenticated_desktop(qmp, serial, rotate_password=False,
                                expect_migration_refusal=False,
                                existing_account=False, migration_cut=None):
    if not existing_account:
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
    if rotate_password:
        send_text(qmp, "passwd")
        press(qmp, "ret", 0.10)
        wait_serial_after(serial, ACCOUNT_CREATED, USERNAME_PROMPT)
        send_text(qmp, CAPTURE_USERNAME)
        press(qmp, "ret", 0.10)
        current_prompt = b"Current password: "
        wait_serial_after(serial, ACCOUNT_CREATED, current_prompt)
        send_text(qmp, CAPTURE_PASSWORD)
        press(qmp, "ret", 0.10)
        wait_serial_after(serial, current_prompt, NEW_PASSWORD_PROMPT,
                          timeout=90.0)
        send_text(qmp, ROTATED_PASSWORD)
        press(qmp, "ret", 0.10)
        wait_serial_after(serial, current_prompt, b"Confirm new password: ")
        send_text(qmp, ROTATED_PASSWORD)
        press(qmp, "ret", 0.10)
        wait_serial_after(serial, current_prompt, PASSWORD_CHANGED,
                          timeout=90.0)
        wait_serial_after(serial, PASSWORD_CHANGED, PROMPT)
    send_text(qmp, "starty")
    press(qmp, "ret", 0.10)
    if rotate_password:
        wait_serial_after(serial, PASSWORD_CHANGED, USERNAME_PROMPT,
                          timeout=30.0)
    else:
        wait_serial(serial, USERNAME_PROMPT, timeout=30.0)
    send_text(qmp, CAPTURE_USERNAME)
    press(qmp, "ret", 0.10)
    wait_serial(serial, PASSWORD_PROMPT, timeout=30.0)
    send_text(qmp, ROTATED_PASSWORD if rotate_password else CAPTURE_PASSWORD)
    press(qmp, "ret", 0.10)
    if migration_cut is not None:
        wait_migrating_and_cut(*migration_cut)
        return
    if expect_migration_refusal:
        refused = b"account: encrypted Data account requires the protected VFS path"
        wait_serial(serial, refused, timeout=90.0)
        send_text(qmp, "ls")
        press(qmp, "ret", 0.10)
        wait_serial_after(serial, refused,
                          b"account: login required; run 'starty'")
        return
    wait_serial(serial, DESKTOP_STARTED, timeout=90.0)


def wait_migrating_and_cut(image, filesystem, process):
    directory_offset = None
    geometry = None
    if filesystem == "fat32":
        raw = image.read_bytes()
        geometry = fat32_image.parse_geometry(raw)
        credentials = [entry for entry in fat32_image.inspect_image(raw)["files"]
                       if entry["path"] == "OPENRFS" and entry["directory"]]
        if len(credentials) != 1:
            raise RuntimeError("FAT32 credential directory is missing")
        directory_offset = geometry.sector_offset(
            geometry.cluster_sector(credentials[0]["first_cluster"]))
    deadline = time.monotonic() + 90.0
    while time.monotonic() < deadline:
        offset = None
        if filesystem == "fat32":
            with image.open("rb") as source:
                source.seek(directory_offset)
                directory = source.read(geometry.bytes_per_sector *
                                        geometry.sectors_per_cluster)
            for at in range(0, len(directory), 32):
                entry = directory[at:at + 32]
                if entry[:11] == b"LOGIN   V2B":
                    high = int.from_bytes(entry[20:22], "little")
                    low = int.from_bytes(entry[26:28], "little")
                    cluster = (high << 16) | low
                    if cluster >= 2:
                        offset = geometry.sector_offset(
                            geometry.cluster_sector(cluster))
                    break
        else:
            result = subprocess.run(("debugfs", "-R",
                                     "blocks /OPENRFS/LOGIN.V2B", str(image)),
                                    capture_output=True, text=True, check=True)
            blocks = result.stdout.split()
            if blocks and blocks[0].isdigit():
                offset = int(blocks[0]) * 4096
        if offset is not None:
            with image.open("rb") as source:
                source.seek(offset)
                record = source.read(224)
            if len(record) == 224 and record[6] == 2:
                process.kill()
                process.wait(timeout=10.0)
                print("cut after observing MIGRATING account record")
                return
        time.sleep(0.05)
    raise RuntimeError("did not observe a durable MIGRATING account record")


def storage_arguments(userspace, system, data, data_filesystem="fat32"):
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
            ("nvme,serial=openrfs-data-" + data_filesystem + ",drive=data-raw,"
             "logical_block_size=" + ("4096" if data_filesystem == "ext4" else "512") +
             ",physical_block_size=" + ("4096" if data_filesystem == "ext4" else "512") +
             ",max_ioqpairs=1,msix_qsize=1"),
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


def verify_ext4_account(image, output, rotated, deleted=False):
    # The guest must have mounted the ext4plus backend and persisted its record.
    report = ext4_image.parse_superblock(image.read_bytes())
    fsck = subprocess.run(["e2fsck", "-fn", str(image)], capture_output=True,
                          text=True, check=False)
    if fsck.returncode != 0:
        raise RuntimeError(f"captured ext4 Data failed e2fsck -fn: {fsck.returncode}\n"
                           + fsck.stdout[-3000:] + fsck.stderr[-1000:])
    expected = "/OPENRFS/LOGIN.V2B" if rotated else "/OPENRFS/LOGIN.V2A"
    stale = ("/OPENRFS/LOGIN.DAT", "/OPENRFS/LOGIN.V2A" if rotated
             else "/OPENRFS/LOGIN.V2B")
    def stat(path):
        result = subprocess.run(["debugfs", "-R", f"stat {path}", str(image)],
                                capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise RuntimeError(f"debugfs could not inspect {path}")
        return result.stdout
    if deleted:
        if any("Inode:" in stat(path) for path in
               ("/OPENRFS/LOGIN.DAT", "/OPENRFS/LOGIN.V2A",
                "/OPENRFS/LOGIN.V2B")):
            raise RuntimeError("deleted ext4 account retained a live record")
    else:
        account = stat(expected)
        if not re.search(r"\bSize:\s*224\b", account):
            raise RuntimeError("captured ext4 account has the wrong record size")
        if any("Inode:" in stat(path) for path in stale):
            raise RuntimeError("captured ext4 Data retained a stale account record")
    with image.open("rb") as source:
        report["sha256"] = hashlib.file_digest(source, "sha256").hexdigest()
    report["e2fsck_clean"] = True
    report["account_path"] = None if deleted else expected
    report["account_size"] = 0 if deleted else 224
    report["account_deleted"] = deleted
    (output / "report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--cpu", default="max")
    parser.add_argument("--iso", required=True)
    parser.add_argument("--userspace")
    parser.add_argument("--system")
    parser.add_argument("--data")
    parser.add_argument("--data-filesystem", choices=("fat32", "ext4"), default="fat32")
    parser.add_argument("--output", required=True)
    parser.add_argument("--rotate-password", action="store_true")
    parser.add_argument("--logout-test", action="store_true")
    parser.add_argument("--encrypted-data-test", action="store_true")
    parser.add_argument("--rotate-after-login", action="store_true")
    parser.add_argument("--native-data-test", action="store_true")
    parser.add_argument("--upload-data-test", action="store_true")
    parser.add_argument("--expect-migration-refusal", action="store_true")
    parser.add_argument("--existing-account", action="store_true")
    parser.add_argument("--expect-unlock-refusal", action="store_true")
    parser.add_argument("--power-cut-migrating", action="store_true")
    parser.add_argument("--disk-full-test", action="store_true")
    deletion = parser.add_mutually_exclusive_group()
    deletion.add_argument("--delete-account", action="store_true")
    deletion.add_argument("--refuse-delete-account", action="store_true")


    args = parser.parse_args()
    if args.userspace is None and args.system is None:
        parser.error("provide --userspace or the --system/--data pair")
    if (args.system is None) != (args.data is None):
        parser.error("--system and --data must be provided together")
    if args.rotate_password and args.data is None:
        parser.error("--rotate-password requires --system and --data")
    if args.logout_test and args.data is None:
        parser.error("--logout-test requires --system and --data")
    if args.encrypted_data_test and args.data is None:
        parser.error("--encrypted-data-test requires --system and --data")
    if args.rotate_after_login and (not args.encrypted_data_test or
                                    args.rotate_password):
        parser.error("--rotate-after-login requires an encrypted Data test")
    if args.native_data_test and not args.encrypted_data_test:
        parser.error("--native-data-test requires an encrypted Data test")
    if args.upload_data_test and not args.encrypted_data_test:
        parser.error("--upload-data-test requires an encrypted Data test")
    if args.existing_account and (args.data is None or args.rotate_password or
                                  args.delete_account or args.refuse_delete_account):
        parser.error("existing account requires a Data image and login")
    if args.expect_unlock_refusal and not args.existing_account:
        parser.error("unlock refusal requires an existing account")
    if args.power_cut_migrating and (args.data is None or args.existing_account or
                                    args.rotate_password or args.encrypted_data_test):
        parser.error("migration cut requires a fresh Data account")
    if args.disk_full_test and (not args.existing_account or
                                args.encrypted_data_test or
                                args.expect_unlock_refusal):
        parser.error("disk-full test requires an existing unlocked account")
    if args.expect_migration_refusal and (
            args.data is None or args.data_filesystem != "ext4" or
            args.rotate_password or args.delete_account or
            args.refuse_delete_account or args.encrypted_data_test or
            args.rotate_after_login):
        parser.error("migration refusal requires an ext4 data image")
    if (args.delete_account or args.refuse_delete_account) and (
            args.data is None or args.data_filesystem != "ext4"):
        parser.error("account deletion gates require ext4 --system and --data")

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
        *storage_arguments(args.userspace, args.system, durable_data, args.data_filesystem),
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
        if args.delete_account or args.refuse_delete_account:
            send_text(qmp, "ls")
            press(qmp, "ret", 0.10)
            wait_serial_after(serial, PROOF_LINE,
                              b"account: create a user first with 'useradd NAME'")
        if durable_data is not None:
            start_authenticated_desktop(qmp, serial, args.rotate_password,
                                        args.expect_migration_refusal or
                                        args.expect_unlock_refusal,
                                        args.existing_account,
                                        (durable_data, args.data_filesystem,
                                         process)
                                        if args.power_cut_migrating else None)
            if (args.expect_migration_refusal or args.expect_unlock_refusal or
                    args.power_cut_migrating):
                return
        time.sleep(0.25)
        # starty opens the minimal desktop with its terminal attached to the
        # production shell. Capture the initial guest frame, then exercise
        # gfetch and a second command through the same PS/2 keyboard path.
        clean = capture(qmp, output, "openrfs-proof")
        send_text(qmp, "gfetch")
        qmp.hmp("sendkey ret")
        wait_serial_after(serial, DESKTOP_STARTED, GFETCH_RESULT)
        if durable_data is not None:
            expected = (b"filesystem  system fat32 ro / data " +
                        (b"ext4plus rw" if args.data_filesystem == "ext4" else b"fat32 rw"))
            wait_serial_after(serial, DESKTOP_STARTED, expected)
        time.sleep(0.20)
        focus = capture(qmp, output, "openrfs-proof-focus")

        send_text(qmp, TERMINAL_COMMAND)
        qmp.hmp("sendkey ret")
        wait_serial_after(serial, DESKTOP_STARTED, TERMINAL_RESULT)
        time.sleep(0.20)
        terminal = capture(qmp, output, "openrfs-proof-terminal")
        if args.rotate_after_login:
            rotate_encrypted_data_password(qmp, serial)
        if args.encrypted_data_test:
            encrypted_data_shell_check(qmp, serial)
        if args.native_data_test:
            native_encrypted_data_check(qmp, serial)
        if args.upload_data_test:
            upload_encrypted_data_check(qmp, serial)
        if args.disk_full_test:
            disk_full_encrypted_data_check(qmp, serial)
            return
        if args.logout_test:
            send_text(qmp, "logout")
            press(qmp, "ret", 0.10)
            ended = b"OpenRFS session ended. Run 'starty' to log in again."
            wait_serial_after(serial, TERMINAL_RESULT, ended)
            send_text(qmp, "ls")
            press(qmp, "ret", 0.10)
            denied = b"account: login required; run 'starty'"
            wait_serial_after(serial, ended, denied)
            send_text(qmp, "starty")
            press(qmp, "ret", 0.10)
            wait_serial_after(serial, denied, USERNAME_PROMPT)
            send_text(qmp, CAPTURE_USERNAME)
            press(qmp, "ret", 0.10)
            wait_serial_after(serial, denied, PASSWORD_PROMPT)
            send_text(qmp, ROTATED_PASSWORD if (
                      args.rotate_password or args.rotate_after_login)
                      else CAPTURE_PASSWORD)
            press(qmp, "ret", 0.10)
            wait_serial_after(serial, denied, b"OpenRFS session resumed.")
        if durable_data is not None:
            root_frame = []
            files_frame = []
            data_frame = []
            capture(qmp, output, "openrfs-proof-wvrm-before", root_frame)
            qmp.hmp("mouse_move 700 500")
            qmp.hmp("mouse_button 4")
            qmp.hmp("mouse_button 0")
            time.sleep(0.40)
            capture(qmp, output, "openrfs-proof-wvrm-menu")
            qmp.hmp("mouse_move -140 -30")
            time.sleep(0.50)
            for attempt in range(3):
                qmp.hmp("mouse_button 1")
                time.sleep(0.10)
                qmp.hmp("mouse_button 0")
                time.sleep(0.60)
                files_frame.clear()
                files = capture(qmp, output, "openrfs-proof-wvrm-files",
                                files_frame)
                at = (300 * files_frame[0][0] + 400) * 3
                if files_frame[0][2][at:at + 3] == b"\xff\xff\xff":
                    break
            else:
                raise RuntimeError("WVRM Files did not open from the root menu")
            qmp.hmp("mouse_move -610 -475")
            time.sleep(0.25)
            for _ in range(2):
                qmp.hmp("mouse_button 1")
                time.sleep(0.06)
                qmp.hmp("mouse_button 0")
                time.sleep(0.10)
            time.sleep(0.50)
            data_view = capture(qmp, output, "openrfs-proof-wvrm-data",
                                data_frame)
            verify_wvrm_files(root_frame[0], files_frame[0], data_frame[0])
            print(files)
            print(data_view)
            if args.delete_account or args.refuse_delete_account:
                # The previous relative move landed on Data at about 273,251.
                # Focus the exposed terminal client at 120,120.
                qmp.hmp("mouse_move -153 -131")
                time.sleep(0.25)
                qmp.hmp("mouse_button 1")
                time.sleep(0.10)
                qmp.hmp("mouse_button 0")
                time.sleep(0.25)
                capture(qmp, output, "openrfs-proof-wvrm-terminal-refocused")
                send_text(qmp, "userdel")
                press(qmp, "ret", 0.10)
                wait_serial_after(serial, TERMINAL_RESULT, USERNAME_PROMPT)
                send_text(qmp, CAPTURE_USERNAME)
                press(qmp, "ret", 0.10)
                wait_serial_after(serial, TERMINAL_RESULT, PASSWORD_PROMPT)
                send_text(qmp, ROTATED_PASSWORD if (
                          args.rotate_password or args.rotate_after_login)
                          else CAPTURE_PASSWORD)
                press(qmp, "ret", 0.10)
                outcome = (b"OpenRFS account removed; Data directories remain."
                           if args.delete_account else
                           b"account: Data still contains files; account deletion refused")
                wait_serial_after(serial, TERMINAL_RESULT, outcome)
                send_text(qmp, "ls")
                press(qmp, "ret", 0.10)
                denied = (b"account: create a user first with 'useradd NAME'"
                          if args.delete_account else
                          b"account: login required; run 'starty'")
                wait_serial_after(serial, outcome, denied)
                time.sleep(0.40)
                revoked_frame = []
                revoked = capture(qmp, output,
                                  "openrfs-proof-wvrm-revoked", revoked_frame)
                verify_wvrm_revoked(data_frame[0], revoked_frame[0])
                print(revoked)
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
    if (PROOF_LINE not in transcript or
            (not args.existing_account and ACCOUNT_CREATED not in transcript) or
            DESKTOP_STARTED not in transcript or GFETCH_RESULT not in transcript or
            TERMINAL_RESULT not in transcript or
            CAPTURE_PASSWORD.encode("ascii") in transcript or
            ROTATED_PASSWORD.encode("ascii") in transcript or
            ((args.rotate_password or args.rotate_after_login) and
             PASSWORD_CHANGED not in transcript) or
            RUNTIME_FAILURE in transcript):
        tail = transcript[-4096:].decode("utf-8", errors="replace")
        raise RuntimeError("proof capture omitted readiness evidence\n" + tail)
    if durable_data is not None and args.data_filesystem == "ext4":
        verify_ext4_account(durable_data, output,
                            args.rotate_password or args.rotate_after_login,
                            args.delete_account)
    if durable_data is not None and args.data_filesystem == "fat32":
        report = fat32_image.inspect_image(durable_data.read_bytes())
        login = [
            item for item in report["files"]
            if item["path"] == ("OPENRFS/LOGIN.V2B" if (
                                 args.rotate_password or args.rotate_after_login)
                                 else "OPENRFS/LOGIN.V2A") and not item["directory"]
        ]
        stale_login = [
            item for item in report["files"]
            if item["path"] in ("OPENRFS/LOGIN.DAT",
                                "OPENRFS/LOGIN.V2A" if (
                                    args.rotate_password or args.rotate_after_login)
                                else "OPENRFS/LOGIN.V2B")
        ]
        if (not bool(report["fat_copies_match"]) or int(report["cycles"]) != 0 or
                int(report["cross_links"]) != 0 or
                int(report["leaked_clusters"]) != 0 or len(login) != 1 or
                stale_login or int(login[0]["size"]) != 224):
            raise RuntimeError("proof capture left an inconsistent account volume")
        (output / "report.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )


if __name__ == "__main__":
    main()
