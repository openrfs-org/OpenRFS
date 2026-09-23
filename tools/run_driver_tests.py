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

Storage scenarios attach a controller with a medium this script fills with a
fixture: every 512-byte unit carries a signature, its own unit number and a
position-dependent byte pattern. The guest must find the medium through the
upstream driver, read the first blocks, a run long enough to need several
transfers, and the last block, and check every byte. On writable media it
rewrites a region, reads it back, and checks its neighbours; after QEMU exits
this script checks the image file itself, so a write is proven on the host
side of the emulated controller, not only by the guest reading its own data.

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
UNIT_BYTES = 512
DISK_UNITS = 32768          # 16 MiB
WRITE_UNITS = 48            # must match DRIVER_TEST_WRITE_UNITS
READ_RUN_BYTES = 160 * 1024  # must match DRIVER_TEST_READ_RUN_BYTES
_RAMP = bytes(range(256)) * 4


def payload(length: int) -> bytes:
    return bytes(((index * 131) ^ (index >> 7) ^ 0x5A) & 0xFF
                 for index in range(length))


def fixture_unit(unit: int, written: bool = False) -> bytes:
    """One 512-byte unit of the storage fixture (see driver_tests.c)."""
    if written:
        head = b"ORFSWRT1" + unit.to_bytes(8, "little")
        body = bytes(((unit * 3) + (offset * 5) + 0x11) & 0xFF
                     for offset in range(16, UNIT_BYTES))
        return head + body
    start = (unit + 16) & 0xFF
    return (b"ORFSBLK1" + unit.to_bytes(8, "little") +
            _RAMP[start:start + UNIT_BYTES - 16])


def write_fixture(path: Path, units: int) -> None:
    with path.open("wb") as image:
        for unit in range(units):
            image.write(fixture_unit(unit))


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
    # Storage: the medium's kind, size in 512-byte units, block size, and
    # whether the guest rewrites part of it. "{image}" in qemu arguments is
    # replaced by the fixture's path.
    kind: str = ""
    units: int = 0
    block_size: int = 512
    write: bool = False
    drivers_option: str = "auto"
    # OpenRFS's own NVMe boot proof reads a fixture block from any NVMe
    # namespace it finds (tools/make-nvme-fixture.py); a scenario that
    # attaches NVMe for an upstream driver carries that block as well.
    native_nvme_fixture: bool = False


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


NATIVE_NVME_BLOCK = 4096
NATIVE_NVME_FIXTURE_LBA = 8


def native_nvme_block() -> bytes:
    return bytes((index * 37 + 11) & 0xFF for index in range(NATIVE_NVME_BLOCK))


def storage_scenario(driver: str, description: str, devices: list[str],
                     kind: str = "disk", machine: str = "pc",
                     units: int = DISK_UNITS, drivers_option: str = "auto",
                     expected_failure: str = "", block_size: int = 0,
                     native_nvme_fixture: bool = False) -> Scenario:
    block_size = block_size or (2048 if kind == "cd" else 512)
    blocks = units * UNIT_BYTES // block_size
    kind_name = {"disk": "disk", "cd": "optical", "fd": "floppy",
                 "sd": "flash"}[kind]
    write = kind != "cd"
    run = min(READ_RUN_BYTES // block_size, blocks // 2)
    markers = [
        rf"^ST DRV blk (disk|cd|fd|sd)[0-9]+ driver {re.escape(driver)} "
        rf"kind {kind_name} block {block_size} blocks {blocks} "
        rf"(read-only|writable) desc .+$",
        rf"^ST DRV blk read lba 0 count {min(8, blocks)} verified$",
        rf"^ST DRV blk read lba {blocks // 2} count {run} verified$",
        rf"^ST DRV blk read lba {blocks - 1} count 1 verified$",
        r"^ST DRV blk range refused$",
        r"^ST DRV blk counters reads [1-9][0-9]* writes [0-9]+ "
        r"blocks-read [1-9][0-9]* blocks-written [0-9]+ errors 0$",
    ]
    if write:
        count = WRITE_UNITS * UNIT_BYTES // block_size
        lba = blocks - 2 * count
        markers += [
            rf"^ST DRV blk read lba {lba} count {count} verified rewritten$",
            rf"^ST DRV blk write lba {lba} count {count} verified$",
        ]
    else:
        markers.append(r"^ST DRV blk write refused read-only$")
    return Scenario(plan="blk", description=description, driver=driver,
                    qemu=devices, markers=markers, machine=machine,
                    kind=kind, units=units, block_size=block_size,
                    write=write, drivers_option=drivers_option,
                    expected_failure=expected_failure,
                    native_nvme_fixture=native_nvme_fixture)


DISK = "if=none,id=drvdisk,file={image},format=raw"
CDROM = "if=none,id=drvdisk,file={image},format=raw,media=cdrom,readonly=on"


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
    "blk-ahci": storage_scenario(
        "ahci", "Intel ICH9 AHCI SATA disk",
        ["-drive", DISK, "-device", "ide-hd,drive=drvdisk,bus=ide.4"],
        machine="q35"),
    "blk-ahci-cd": storage_scenario(
        "ahci", "Intel ICH9 AHCI SATA ATAPI CD-ROM",
        ["-drive", CDROM, "-device", "ide-cd,drive=drvdisk,bus=ide.4"],
        kind="cd", machine="q35"),
    "blk-ata": storage_scenario(
        "ata", "Intel PIIX3 IDE ATA disk (primary master)",
        ["-drive", DISK, "-device", "ide-hd,drive=drvdisk,bus=ide.0,unit=0"]),
    "blk-ata-cd": storage_scenario(
        "ata", "Intel PIIX3 IDE ATAPI CD-ROM (primary slave)",
        ["-drive", CDROM, "-device", "ide-cd,drive=drvdisk,bus=ide.0,unit=1"],
        kind="cd"),
    "blk-virtio-blk": storage_scenario(
        "virtio-blk", "virtio-blk (transitional)",
        ["-drive", DISK, "-device", "virtio-blk-pci,drive=drvdisk"]),
    "blk-virtio-blk-legacy": storage_scenario(
        "virtio-blk", "virtio-blk (legacy 0.9.5 interface)",
        ["-drive", DISK,
         "-device", "virtio-blk-pci,drive=drvdisk,disable-modern=on"]),
    "blk-virtio-blk-modern": storage_scenario(
        "virtio-blk", "virtio-blk (virtio 1.0 interface only)",
        ["-drive", DISK,
         "-device", "virtio-blk-pci,drive=drvdisk,disable-legacy=on"],
        machine="q35"),
    "blk-virtio-scsi": storage_scenario(
        "virtio-scsi", "virtio-scsi disk",
        ["-device", "virtio-scsi-pci,id=hba", "-drive", DISK,
         "-device", "scsi-hd,drive=drvdisk,bus=hba.0"]),
    "blk-virtio-scsi-cd": storage_scenario(
        "virtio-scsi", "virtio-scsi CD-ROM",
        ["-device", "virtio-scsi-pci,id=hba", "-drive", CDROM,
         "-device", "scsi-cd,drive=drvdisk,bus=hba.0"],
        kind="cd"),
    "blk-lsi": storage_scenario(
        "lsi-scsi", "LSI 53C895A Ultra2 SCSI disk",
        ["-device", "lsi53c895a,id=hba", "-drive", DISK,
         "-device", "scsi-hd,drive=drvdisk,bus=hba.0"]),
    "blk-esp": storage_scenario(
        "esp-scsi", "AMD Am53C974 PCscsi disk",
        ["-device", "am53c974,id=hba", "-drive", DISK,
         "-device", "scsi-hd,drive=drvdisk,bus=hba.0"]),
    "blk-dc390": storage_scenario(
        "esp-scsi", "Tekram DC-390 (Am53C974) disk",
        ["-device", "dc390,id=hba", "-drive", DISK,
         "-device", "scsi-hd,drive=drvdisk,bus=hba.0"]),
    "blk-megasas": storage_scenario(
        "megasas", "LSI MegaRAID SAS 1078 disk",
        ["-device", "megasas,id=hba", "-drive", DISK,
         "-device", "scsi-hd,drive=drvdisk,bus=hba.0"]),
    "blk-megasas-gen2": storage_scenario(
        "megasas", "LSI MegaRAID SAS 2108 disk",
        ["-device", "megasas-gen2,id=hba", "-drive", DISK,
         "-device", "scsi-hd,drive=drvdisk,bus=hba.0"]),
    "blk-mptsas": storage_scenario(
        "mpt-scsi", "LSI SAS1068 Fusion-MPT disk",
        ["-device", "mptsas1068,id=hba", "-drive", DISK,
         "-device", "scsi-hd,drive=drvdisk,bus=hba.0"]),
    "blk-pvscsi": storage_scenario(
        "pvscsi", "VMware PVSCSI disk",
        ["-device", "pvscsi,id=hba", "-drive", DISK,
         "-device", "scsi-hd,drive=drvdisk,bus=hba.0"]),
    "blk-sdhci": storage_scenario(
        "sdcard", "SD Host Controller (SDHCI 3.0) with SD card",
        ["-device", "sdhci-pci", "-drive", DISK,
         "-device", "sd-card,drive=drvdisk"],
        kind="sd"),
    "blk-floppy": storage_scenario(
        "floppy", "82077AA floppy controller, 1.44 MB 3.5-inch diskette",
        ["-drive", DISK,
         "-device", "floppy,unit=0,drive=drvdisk,drive-type=144"],
        kind="fd", units=2880),
    "blk-nvme": storage_scenario(
        "nvme", "NVM Express controller (SeaBIOS driver, selected)",
        ["-drive", DISK,
         "-device", "nvme,serial=openrfs0,drive=drvdisk,"
                    "logical_block_size=4096,physical_block_size=4096"],
        drivers_option="nvme", block_size=4096, native_nvme_fixture=True,
        # 12 MiB: OpenRFS's FAT16 proof takes a 16 MiB 4 KiB-block
        # namespace for its own fixture.
        units=24576),
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
    options = ["openrfs.test=drivers",
               f"openrfs.drivers={scenario.drivers_option}",
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
    image = work / ("medium.iso" if scenario.kind == "cd" else "medium.img")
    if scenario.plan == "blk":
        write_fixture(image, scenario.units)
        if scenario.native_nvme_fixture:
            with image.open("r+b") as medium:
                medium.seek(NATIVE_NVME_FIXTURE_LBA * NATIVE_NVME_BLOCK)
                medium.write(native_nvme_block())
        options += [f"openrfs.drvkind={scenario.kind}",
                    f"openrfs.drvunits={scenario.units}",
                    f"openrfs.drvwrite={1 if scenario.write else 0}"]
    qemu += [argument.replace("{image}", str(image))
             for argument in scenario.qemu]
    iso = build_iso(args.kernel, work, " ".join(options), args.grub_mkrescue)
    qemu += ["-cdrom", str(iso)] + args.qemu_arg
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
    if scenario.plan == "blk" and not problems:
        problems += check_medium(image, scenario)
    evidence = [line for line in lines if line.startswith("ST DRV")]
    (work / "evidence.txt").write_text("\n".join(evidence) + "\n")
    return not problems, "; ".join(problems)


def check_medium(image: Path, scenario: Scenario) -> list[str]:
    """Check the image QEMU wrote: rewritten units and their neighbours."""
    problems = []
    data = image.read_bytes()
    units = scenario.units
    if len(data) != units * UNIT_BYTES:
        return [f"medium size changed to {len(data)} bytes"]

    def unit_bytes(unit: int) -> bytes:
        return data[unit * UNIT_BYTES:(unit + 1) * UNIT_BYTES]

    first = units - 2 * WRITE_UNITS if scenario.write else units
    for unit in range(first, first + (WRITE_UNITS if scenario.write else 0)):
        if unit_bytes(unit) != fixture_unit(unit, written=True):
            problems.append(f"host image unit {unit} was not rewritten")
            break
    for unit in (0, units // 2, first - 1, first + WRITE_UNITS, units - 1):
        if 0 <= unit < units and (not scenario.write or
                                  not first <= unit < first + WRITE_UNITS):
            if unit_bytes(unit) != fixture_unit(unit):
                problems.append(f"host image unit {unit} changed")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--grub-mkrescue", default="grub-mkrescue")
    parser.add_argument("--accel", default="tcg")
    parser.add_argument("--scenario", action="append", default=[])
    parser.add_argument("--qemu-arg", action="append", default=[],
                        help="extra QEMU argument, e.g. for -trace")
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
