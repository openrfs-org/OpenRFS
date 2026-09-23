#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the OpenRFS upstream driver suite.

Every scenario boots the kernel once in QEMU with one device profile attached
and one in-guest plan selected (openrfs.test=drivers openrfs.drvtest=<plan>).
A scenario passes only when QEMU exits through the debug-exit device with the
suite's status, the serial log carries "ST PASS drivers" exactly once, no
failure or panic marker appears, and every marker the scenario requires is
present.

Network scenarios attach the NIC to QEMU's user-mode network. The guest must
obtain a DHCP lease, answer ICMP through the gateway, and download a
deterministic payload over HTTP from a server this script runs on the host;
the kernel regenerates the payload and compares it byte for byte. Every frame
is captured to a pcap file beside the serial log.

These are emulated devices. QEMU models the real parts' register interfaces,
which is what the drivers program, but a pass here is evidence about those
models, not about physical hardware.
"""

from __future__ import annotations

import argparse
import http.server
import re
import shutil
import socket
import subprocess
import sys
import threading
from dataclasses import dataclass, field
from pathlib import Path

SUITE_EXIT_VALUE = 0x88
SUITE_EXIT_STATUS = ((SUITE_EXIT_VALUE << 1) | 1) & 0xFF
DEFAULT_PAYLOAD_BYTES = 262144
GUEST_MAC = "52:54:00:12:34:56"


def payload(length: int) -> bytes:
    return bytes(((index * 131) ^ (index >> 7) ^ 0x5A) & 0xFF
                 for index in range(length))


@dataclass
class Scenario:
    plan: str
    description: str
    driver: str = ""
    nic_model: str = ""
    qemu: list[str] = field(default_factory=list)
    markers: list[str] = field(default_factory=list)
    timeout: int = 120
    machine: str = "pc"
    # A scenario QEMU's model cannot pass for a reason outside the driver;
    # it runs and is reported, but does not count against the suite.
    expected_failure: str = ""


def network_scenario(model: str, driver: str, description: str,
                     machine: str = "pc", expected_failure: str = ""
                     ) -> Scenario:
    return Scenario(
        plan="net",
        description=description,
        driver=driver,
        nic_model=model,
        machine=machine,
        expected_failure=expected_failure,
        markers=[
            rf"^ST DRV net0 driver {re.escape(driver)} mac "
            rf"{re.escape(GUEST_MAC)} link up$",
            r"^ST DRV dhcp address 10\.0\.2\.15 gateway 10\.0\.2\.2$",
            r"^ST DRV ping sent 3 received [1-3]$",
            rf"^ST DRV http bytes {DEFAULT_PAYLOAD_BYTES} verified$",
            r"^ST DRV counters rx [1-9][0-9]* tx [1-9][0-9]* dropped [0-9]+$",
        ],
    )


SCENARIOS: dict[str, Scenario] = {
    "net-e1000": network_scenario(
        "e1000", "intel", "Intel 82540EM Gigabit Ethernet (e1000)"),
    "net-e1000-82544gc": network_scenario(
        "e1000-82544gc", "intel", "Intel 82544GC Gigabit Ethernet"),
    "net-e1000-82545em": network_scenario(
        "e1000-82545em", "intel", "Intel 82545EM Gigabit Ethernet"),
    "net-e1000e": network_scenario(
        "e1000e", "intel", "Intel 82574L Gigabit Ethernet (e1000e)",
        machine="q35"),
    # QEMU 8.2's igb model implements only what Linux's igb driver uses:
    # advanced receive descriptors ("igb_wrn_rx_desc_modes_not_supp
    # Not supported descriptor type: 0" for the legacy format the 82576
    # datasheet also defines), a transmit-done cause raised only through an
    # IVAR/MSI-X vector mapping, and no autonegotiation after a software reset
    # (hw/net/igb_core.c: igb_reset, igb_set_ctrl, igb_tx_wb_eic). iPXE's
    # driver uses legacy descriptors and polls ICR, as real 82576 silicon
    # permits, so this model cannot carry its traffic.
    "net-igb": network_scenario(
        "igb", "intel", "Intel 82576 Gigabit Ethernet (igb)", machine="q35",
        expected_failure="QEMU 8.2 igb model lacks legacy descriptors and "
                         "polled ICR causes"),
    "net-i82557b": network_scenario(
        "i82557b", "eepro100", "Intel 82557B PRO/100"),
    "net-i82559er": network_scenario(
        "i82559er", "eepro100", "Intel 82559ER PRO/100"),
    "net-i82550": network_scenario(
        "i82550", "eepro100", "Intel 82550 PRO/100"),
    "net-rtl8139": network_scenario(
        "rtl8139", "realtek", "Realtek RTL8139C+ Fast Ethernet"),
    "net-pcnet": network_scenario(
        "pcnet", "pcnet32", "AMD Am79C970A PCnet-PCI II"),
    "net-ne2k-pci": network_scenario(
        "ne2k_pci", "ne2k-pci", "Realtek RTL8029 NE2000 PCI"),
    "net-tulip": network_scenario(
        "tulip", "tulip", "DEC 21143 Tulip"),
    "net-vmxnet3": network_scenario(
        "vmxnet3", "vmxnet3", "VMware VMXNET3", machine="q35"),
}


class PayloadHandler(http.server.BaseHTTPRequestHandler):
    # The OpenRFS HTTP client speaks exactly HTTP/1.1.
    protocol_version = "HTTP/1.1"
    body = b""

    def do_GET(self) -> None:  # noqa: N802 - http.server naming
        if self.path != "/payload.bin":
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(self.body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(self.body)

    def log_message(self, format: str, *args: object) -> None:
        return


def start_server(length: int) -> tuple[http.server.HTTPServer, int]:
    handler = type("Handler", (PayloadHandler,), {"body": payload(length)})
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, server.server_address[1]


def build_iso(kernel: Path, work: Path, command_line: str,
              grub_mkrescue: str) -> Path:
    root = work / "iso-root"
    if root.exists():
        shutil.rmtree(root)
    (root / "boot" / "grub").mkdir(parents=True)
    shutil.copy2(kernel, root / "boot" / "openrfs.elf")
    (root / "boot" / "grub" / "grub.cfg").write_text(
        "set default=0\nset timeout=0\n\n"
        'menuentry "OpenRFS driver suite" {\n'
        f"    multiboot2 /boot/openrfs.elf {command_line}\n"
        "    boot\n}\n")
    iso = work / "openrfs.iso"
    subprocess.run([grub_mkrescue, "-o", str(iso), str(root)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return iso


def run_scenario(name: str, scenario: Scenario, args: argparse.Namespace
                 ) -> tuple[bool, str]:
    work = args.output / name
    work.mkdir(parents=True, exist_ok=True)
    log = work / "serial.log"
    options = ["openrfs.test=drivers", "openrfs.drivers=auto",
               f"openrfs.drvtest={scenario.plan}"]
    server = None
    qemu = [args.qemu, "-machine", f"{scenario.machine},accel={args.accel}",
            "-m", "256M", "-smp", "1", "-display", "none",
            "-monitor", "none", "-serial", f"file:{log}",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
            "-no-reboot"]
    if scenario.driver:
        options.append(f"openrfs.drvdriver={scenario.driver}")
    if scenario.plan == "net":
        server, port = start_server(DEFAULT_PAYLOAD_BYTES)
        options += [f"openrfs.drvport={port}",
                    f"openrfs.drvbytes={DEFAULT_PAYLOAD_BYTES}"]
        qemu += ["-nic", "none",
                 "-netdev", "user,id=drvnet,restrict=off",
                 "-device", f"{scenario.nic_model},netdev=drvnet,"
                            f"mac={GUEST_MAC}",
                 "-object", f"filter-dump,id=drvdump,netdev=drvnet,"
                            f"file={work / 'capture.pcap'}"]
    else:
        qemu += ["-nic", "none"]
    qemu += scenario.qemu
    iso = build_iso(args.kernel, work, " ".join(options), args.grub_mkrescue)
    qemu += ["-cdrom", str(iso)]
    if log.exists():
        log.unlink()
    try:
        process = subprocess.Popen(qemu, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE)
        try:
            _, stderr_bytes = process.communicate(timeout=scenario.timeout)
            status = process.returncode
            stderr = stderr_bytes.decode(errors="replace")
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            status = None
            stderr = "timeout"
    finally:
        if server is not None:
            server.shutdown()
            server.server_close()
    text = log.read_text(errors="replace") if log.exists() else ""
    lines = text.splitlines()
    problems = []
    if status != SUITE_EXIT_STATUS:
        problems.append(f"exit status {status} (expected "
                        f"{SUITE_EXIT_STATUS}) {stderr.strip()}")
    if lines.count("ST PASS drivers") != 1:
        problems.append("missing ST PASS drivers")
    if any(line.startswith("ST FAIL") for line in lines):
        problems.append("ST FAIL present")
    if any("OpenRFS PANIC" in line for line in lines):
        problems.append("kernel panic")
    for marker in scenario.markers:
        pattern = re.compile(marker)
        if not any(pattern.search(line) for line in lines):
            problems.append(f"missing marker {marker}")
    evidence = [line for line in lines if line.startswith("ST DRV")]
    (work / "evidence.txt").write_text("\n".join(evidence) + "\n")
    return not problems, "; ".join(problems)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--grub-mkrescue", default="grub-mkrescue")
    parser.add_argument("--accel", default="tcg")
    parser.add_argument("--scenario", action="append", default=[])
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    if args.list:
        for name, scenario in SCENARIOS.items():
            print(f"{name}: {scenario.description}")
        return 0
    names = args.scenario or list(SCENARIOS)
    unknown = [name for name in names if name not in SCENARIOS]
    if unknown:
        print(f"unknown scenario(s): {', '.join(unknown)}", file=sys.stderr)
        return 2
    args.output.mkdir(parents=True, exist_ok=True)
    failures = 0
    counted = 0
    passes = 0
    for name in names:
        scenario = SCENARIOS[name]
        passed, detail = run_scenario(name, scenario, args)
        if scenario.expected_failure:
            label = "XPASS" if passed else "XFAIL"
            print(f"{label} {name}: {scenario.description} -- "
                  f"{scenario.expected_failure}", flush=True)
            continue
        counted += 1
        passes += 1 if passed else 0
        print(f"{'PASS' if passed else 'FAIL'} {name}: "
              f"{scenario.description}"
              + ("" if passed else f" -- {detail}"), flush=True)
        failures += 0 if passed else 1
    print(f"driver suite: {passes}/{counted} scenarios passed", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
