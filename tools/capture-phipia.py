#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture a functional Phipia desktop session from QEMU.

The recording begins only after the installed Boot Ledger proof and shell
prompt. Frames come from QEMU's emulated display through QMP, pointer clicks
and keystrokes travel through the ordinary PS/2 guest input path, and the
attached FAT32 data image is retained beside the evidence. The scripted
session exercises the taskbar, real window state, Notes formatting,
Settings, Store, and Task Manager.
"""

import argparse
import json
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import zlib
from pathlib import Path

import fat32_image


PROOF_LINE = b"Phipia: Boot Ledger installed proof passed"
PROMPT = b"phip> "
RUNTIME_FAILURE = b"runtime disabled"
WIDTH = 1024
HEIGHT = 768
DOCK_FIXED_ONE = 65536
DOCK_ICON_SIZE = 58
DOCK_GAP_FACTOR = 10486
DOCK_ITEM_COUNT = 5
DOCK_POINTER_Y = 748
DOCK_FILES = 0
DOCK_TERMINAL = 1
DOCK_NOTES = 2
DOCK_STORE = 3
DOCK_SETTINGS = 4


def dock_item_center(index):
    """Return the center of a Phipia taskbar application at 1024x768."""
    if index < 0 or index >= DOCK_ITEM_COUNT:
        raise ValueError("taskbar application index is outside the layout")
    # Start is 48 pixels and the search box is 320. Application slots are
    # contiguous 40-pixel squares in the left-aligned Windows 10 layout.
    return 48 + 320 + index * 40 + 20


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
            newline = data.find(b"\n", position)
            if newline < 0:
                raise RuntimeError("QEMU PPM comment is unterminated")
            position = newline + 1
            continue
        end = position
        while end < len(data) and data[end] not in b" \t\r\n":
            end += 1
        tokens.append(data[position:end])
        position = end
    if tokens[0] != b"P6" or tokens[3] != b"255":
        raise RuntimeError("QEMU screendump is not an 8-bit binary PPM")
    width, height = int(tokens[1]), int(tokens[2])
    while position < len(data) and data[position] in b" \t\r\n":
        position += 1
    pixels = data[position:]
    if len(pixels) != width * height * 3:
        raise RuntimeError("QEMU screendump pixel body is truncated")
    rows = b"".join(
        b"\x00" + pixels[y * width * 3:(y + 1) * width * 3]
        for y in range(height)
    )
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(
        ">IIBBBBB", width, height, 8, 2, 0, 0, 0
    ))
    png += png_chunk(b"IDAT", zlib.compress(rows, 9))
    png += png_chunk(b"IEND", b"")
    Path(destination).write_bytes(png)


class Qmp:
    def __init__(self, port):
        deadline = time.monotonic() + 10.0
        while True:
            try:
                self.socket = socket.create_connection(
                    ("127.0.0.1", port), 0.5
                )
                self.socket.settimeout(None)
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
        return self.execute(
            "human-monitor-command", {"command-line": command}
        )

    def close(self):
        self.file.close()
        self.socket.close()


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def wait_serial(path, markers, timeout=90.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        transcript = path.read_bytes() if path.exists() else b""
        if all(marker in transcript for marker in markers):
            return
        time.sleep(0.05)
    transcript = path.read_bytes() if path.exists() else b""
    tail = transcript[-8192:].decode("utf-8", errors="replace")
    raise RuntimeError(f"guest readiness markers were omitted\n{tail}")


def storage_arguments(system, data):
    return [
        "-blockdev",
        f"driver=file,filename={system.resolve()},node-name=system-file,read-only=on,auto-read-only=off",
        "-blockdev",
        "driver=raw,file=system-file,node-name=system-raw,read-only=on",
        "-device",
        "nvme,serial=phipia-system-fat32,drive=system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1",
        "-blockdev",
        f"driver=file,filename={data.resolve()},node-name=data-file,read-only=off,auto-read-only=off",
        "-blockdev",
        "driver=raw,file=data-file,node-name=data-raw,read-only=off",
        "-device",
        "nvme,serial=phipia-data-fat32,drive=data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1",
    ]


def capture_ppm(qmp, destination):
    qmp.execute("screendump", {
        "filename": destination.resolve().as_posix(), "format": "ppm"
    })


def capture_png(qmp, work, output, name):
    ppm = work / f"{name}.ppm"
    destination = output / f"{name}.png"
    capture_ppm(qmp, ppm)
    ppm_to_png(ppm, destination)
    ppm.unlink()


def send_text(qmp, text, delay=0.040):
    names = {" ": "spc", "-": "minus", ".": "dot", "/": "slash"}
    for character in text:
        if "A" <= character <= "Z":
            key = f"shift-{character.lower()}"
        else:
            key = names.get(character, character)
        qmp.hmp(f"sendkey {key} 15")
        time.sleep(delay)


class Pointer:
    def __init__(self, qmp):
        self.qmp = qmp
        self.x = WIDTH - WIDTH // 4
        self.y = HEIGHT // 3

    def move_to(self, x, y):
        while self.x != x or self.y != y:
            dx = max(-40, min(40, x - self.x))
            dy = max(-40, min(40, y - self.y))
            self.qmp.hmp(f"mouse_move {dx} {dy}")
            self.x += dx
            self.y += dy
            time.sleep(0.025)
        time.sleep(0.10)

    def prime_terminal(self):
        # QEMU's relative PS/2 path needs one ordinary large host motion before
        # it begins emitting the smaller packets used for the scripted path.
        self.qmp.hmp("mouse_move -260 320")
        time.sleep(0.25)
        self.qmp.hmp("mouse_move 4 120")
        self.x = 512
        self.y = 696
        time.sleep(0.45)
        self.move_to(dock_item_center(DOCK_TERMINAL), DOCK_POINTER_Y)

    def rehome(self):
        """Clamp guest and script coordinates back to the same origin."""
        for _ in range(14):
            self.qmp.hmp("mouse_move -80 -80")
            time.sleep(0.025)
        self.x = 0
        self.y = 0
        time.sleep(0.15)

    def click(self):
        self.qmp.hmp("mouse_button 1")
        time.sleep(0.05)
        self.qmp.hmp("mouse_button 0")
        time.sleep(0.08)

    def drag_to(self, start_x, start_y, end_x, end_y):
        self.move_to(start_x, start_y)
        self.qmp.hmp("mouse_button 1")
        time.sleep(0.08)
        self.move_to(end_x, end_y)
        time.sleep(0.08)
        self.qmp.hmp("mouse_button 0")
        time.sleep(0.12)

    def settle_guest(self, delay=0.35):
        """Let the guest drain input, then wake one final redraw."""
        time.sleep(delay)
        self.qmp.hmp("mouse_move 1 0")
        self.x += 1
        time.sleep(0.12)
        self.qmp.hmp("mouse_move -1 0")
        self.x -= 1
        time.sleep(0.18)


PHIPIA_REQUIRED_EVENTS = {
    "taskbar",
    "files_windowed",
    "files_maximized",
    "files_minimized",
    "files_restored",
    "notes_formatted",
    "settings",
    "store",
    "task_manager",
    "desktop_restored",
}


def capture_phipia_session(
    args, qmp, pointer, work, output, durable_data, serial
):
    """Exercise Phipia through guest input and retain each visible result."""
    frames = []
    capture_times = []
    events = set()

    def snapshot(name, event):
        frame = work / f"phipia-frame-{len(frames):04d}.ppm"
        capture_ppm(qmp, frame)
        ppm_to_png(frame, output / f"{name}.png")
        frames.append(frame)
        capture_times.append(time.monotonic())
        events.add(event)

    def open_app(index, delay=0.85):
        # Caption controls at the extreme right edge can clamp a relative
        # PS/2 packet before the script's coordinate accumulator observes it.
        # Re-establish a shared origin before every taskbar launch so a later
        # click cannot silently drift into the neighbouring application.
        pointer.rehome()
        pointer.move_to(dock_item_center(index), DOCK_POINTER_Y)
        pointer.click()
        pointer.settle_guest(delay)

    pointer.prime_terminal()
    snapshot("phipia-taskbar", "taskbar")

    # Files is the first window and therefore starts at the home frame
    # (82,40 860x602). Exercise every caption control against real window
    # state, including restoring a maximized window from the taskbar.
    open_app(DOCK_FILES)
    snapshot("phipia-files-windowed", "files_windowed")
    pointer.move_to(873, 56)
    pointer.click()
    pointer.settle_guest(0.45)
    snapshot("phipia-files-maximized", "files_maximized")
    pointer.move_to(909, 16)
    pointer.click()
    pointer.settle_guest(0.55)
    snapshot("phipia-files-minimized", "files_minimized")
    open_app(DOCK_FILES, 0.55)
    snapshot("phipia-files-restored", "files_restored")
    pointer.move_to(955, 16)
    pointer.click()
    pointer.settle_guest(0.35)
    pointer.move_to(919, 56)
    pointer.click()
    pointer.settle_guest(0.35)

    # Notes is the second window at (96,51). Maximize it, make a second note,
    # enter visible text, toggle real bold and italic state, then persist it.
    open_app(DOCK_NOTES)
    pointer.move_to(887, 67)
    pointer.click()
    pointer.settle_guest(0.40)
    pointer.move_to(27, 21)
    pointer.click()
    send_text(qmp, "Massive Phipia update. Notes formatting is live.", 0.012)
    pointer.move_to(257, 707)
    pointer.click()
    pointer.move_to(289, 707)
    pointer.click()
    qmp.hmp("sendkey ctrl-s")
    pointer.settle_guest(0.55)
    snapshot("phipia-notes-formatted", "notes_formatted")
    pointer.move_to(1001, 16)
    pointer.click()
    pointer.settle_guest(0.35)

    open_app(DOCK_SETTINGS)
    snapshot("phipia-settings", "settings")
    open_app(DOCK_STORE)
    snapshot("phipia-store", "store")

    # Ctrl+Shift+Esc is delivered as a real PS/2 chord. Task Manager builds
    # its process table from the live Phipia window census.
    qmp.hmp("sendkey ctrl-shift-esc")
    pointer.settle_guest(0.70)
    snapshot("phipia-task-manager", "task_manager")

    # The six-pixel show-desktop strip minimizes every open window without
    # destroying it, leaving the running indicators on the taskbar.
    pointer.move_to(WIDTH - 2, DOCK_POINTER_Y)
    pointer.click()
    pointer.settle_guest(0.55)
    snapshot("phipia-desktop-restored", "desktop_restored")
    return events, frames, capture_times


def encode(ffmpeg, frames, capture_times, fps, seconds, output):
    if not frames or len(frames) != len(capture_times):
        raise RuntimeError("video frame timing evidence is incomplete")
    origin = capture_times[0]
    normalized = [timestamp - origin for timestamp in capture_times]
    manifest = frames[0].parent / "frames.ffconcat"
    lines = ["ffconcat version 1.0"]
    for index, frame in enumerate(frames):
        if index + 1 < len(frames):
            duration = normalized[index + 1] - normalized[index]
        else:
            duration = seconds - normalized[index]
        duration = max(0.001, duration)
        lines.append(f"file '{frame.resolve().as_posix()}'")
        lines.append(f"duration {duration:.9f}")
    lines.append(f"file '{frames[-1].resolve().as_posix()}'")
    manifest.write_text("\n".join(lines) + "\n", encoding="ascii")
    frame_count = int(round(seconds * fps))
    subprocess.run([
        ffmpeg, "-hide_banner", "-loglevel", "warning", "-y",
        "-f", "concat", "-safe", "0", "-i", str(manifest),
        "-vf", "setpts=PTS-STARTPTS,format=yuv420p,"
               "tpad=stop_mode=clone:stop_duration=12",
        "-c:v", "libx264", "-preset", "medium",
        "-crf", "18", "-r", str(fps), "-frames:v", str(frame_count),
        "-movflags", "+faststart", str(output)
    ], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument(
        "--accel", choices=("tcg", "whpx"), default="tcg",
        help="QEMU accelerator used for the evidence boot",
    )
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--iso", required=True)
    parser.add_argument("--system", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--seconds", type=float, default=25.0)
    parser.add_argument("--fps", type=int, default=8)
    args = parser.parse_args()
    if args.seconds < 25.0:
        parser.error("the UI application proof needs at least 25 seconds")
    if args.fps <= 0:
        parser.error("--fps must be positive")

    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    durable_data = output / "phipia-data.raw"
    shutil.copyfile(Path(args.data).resolve(), durable_data)
    populated = fat32_image.populate_data_files(durable_data.read_bytes(), [
        ("NOTES.TXT", b"Ready for a Phipia note."),
    ])
    fat32_image.atomic_write(durable_data, populated)
    serial = output / "phipia-serial.log"
    if serial.exists():
        serial.unlink()
    video = output / "phipia-massive-update-25s.mp4"
    port = free_port()
    command = [
        args.qemu, "-machine", f"accel={args.accel}", "-m", "128M",
        "-smp", "1",
        "-boot", "order=d", "-cdrom", str(Path(args.iso).resolve()),
        "-display", "none",
        *storage_arguments(Path(args.system).resolve(), durable_data),
        "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
        "-serial", f"file:{serial}", "-no-reboot"
    ]

    with tempfile.TemporaryDirectory(prefix="phipia-capture-") as raw:
        work = Path(raw)
        process = subprocess.Popen(
            command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        qmp = None
        try:
            qmp = Qmp(port)
            wait_serial(serial, (PROOF_LINE, PROMPT))
            time.sleep(0.35)
            pointer = Pointer(qmp)
            capture_png(qmp, work, output, "phipia-desktop")

            events, captured_frames, capture_times = capture_phipia_session(
                args, qmp, pointer, work, output, durable_data, serial
            )
            required = PHIPIA_REQUIRED_EVENTS
            if events != required:
                raise RuntimeError(f"capture omitted interactions: {required - events}")
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
                RUNTIME_FAILURE in transcript):
            tail = transcript[-4096:].decode("utf-8", errors="replace")
            raise RuntimeError(
                "recording omitted the installed proof/prompt or disabled "
                "the Phipia runtime\n" + tail
            )

        report = fat32_image.inspect_image(durable_data.read_bytes())
        files = {
            str(record["path"]): record
            for record in report["files"]
            if not bool(record["directory"])
        }
        if ("NOTES.TXT" not in files or
                int(files["NOTES.TXT"]["size"]) <=
                len(b"Ready for a Phipia note.")):
            raise RuntimeError("guest evidence omitted the saved Notes document")
        if (not bool(report["fat_copies_match"]) or
                int(report["cycles"]) != 0 or
                int(report["cross_links"]) != 0 or
                int(report["leaked_clusters"]) != 0):
            raise RuntimeError("guest evidence left an inconsistent FAT32 image")
        (output / "report.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        encode(args.ffmpeg, captured_frames, capture_times, args.fps,
               args.seconds, video)

    print(video)


if __name__ == "__main__":
    main()
