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
import hashlib
import http.server
import json
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

SUITE_EXIT_VALUE = 0x88
SUITE_EXIT_STATUS = ((SUITE_EXIT_VALUE << 1) | 1) & 0xFF
DEFAULT_PAYLOAD_BYTES = 262144
GUEST_MAC = "52:54:00:12:34:56"
# QEMU's usb-net reports its address to CDC-ECM hosts, in the iMACAddress
# string, with the first octet replaced by 0x40 (hw/usb/dev-network.c,
# usb_net_realize); its RNDIS OID answers carry GUEST_MAC unchanged.
USB_ECM_MAC = "40" + GUEST_MAC[2:]
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
    # HID: what the runner types once the guest says it is ready.
    text: str = ""
    # Display: the mode the guest sets (WIDTHxHEIGHTxBPP) and the QEMU
    # device whose screen the runner captures.
    mode: str = ""
    screen: str = ""
    # Network: devices the NIC attaches to (a USB host controller, a hub),
    # which QEMU must create before the NIC itself.
    nic_bus: list[str] = field(default_factory=list)


def network_scenario(model: str, driver: str, description: str,
                     machine: str = "pc", expected_failure: str = "",
                     drivers_option: str = "auto",
                     nic_bus: tuple[str, ...] = (),
                     mac: str = GUEST_MAC) -> Scenario:
    return Scenario(
        plan="net",
        description=description,
        driver=driver,
        nic_model=model,
        machine=machine,
        expected_failure=expected_failure,
        drivers_option=drivers_option,
        nic_bus=list(nic_bus),
        markers=[
            rf"^ST DRV net0 driver {re.escape(driver)} mac "
            rf"{re.escape(mac)} link up$",
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
                     native_nvme_fixture: bool = False,
                     extra_markers: tuple[str, ...] = (),
                     timeout: int = 120) -> Scenario:
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
    markers += list(extra_markers)
    return Scenario(plan="blk", description=description, driver=driver,
                    qemu=devices, markers=markers, machine=machine,
                    timeout=timeout,
                    kind=kind, units=units, block_size=block_size,
                    write=write, drivers_option=drivers_option,
                    expected_failure=expected_failure,
                    native_nvme_fixture=native_nvme_fixture)


def hid_scenario(kind: str, description: str, devices: list[str],
                 host: str, machine: str = "pc", text: str = "openrfs"
                 ) -> Scenario:
    markers = [r"^ST DRV hid ready$", usb_host_marker(host)]
    if kind in ("kbd", "both"):
        markers += [
            r"^OpenRFS: kbd0 bound by SeaBIOS usb-hid: USB HID boot keyboard$",
            # Every key pressed and released, all through the USB driver,
            # none through the i8042.
            rf"^ST DRV hid keys {text} usb-bytes [1-9][0-9]* "
            r"i8042-interrupts 0$",
        ]
    if kind in ("mouse", "both"):
        markers += [
            r"^OpenRFS: mouse0 bound by SeaBIOS usb-hid: USB HID boot mouse$",
            r"^ST DRV hid pointer dx [1-9][0-9]* dy [1-9][0-9]* "
            r"button-transitions [2-9][0-9]* usb-packets [1-9][0-9]* "
            r"i8042-interrupts 0$",
        ]
    return Scenario(plan="hid", description=description, qemu=devices,
                    markers=markers, machine=machine, kind=kind, text=text)


DISPLAY_BOGUS_MODE = "1234x567"   # must match driver_tests.c


def display_scenario(driver: str, description: str, device: str, mode: str,
                     machine: str = "pc", timeout: int = 180) -> Scenario:
    """One adapter as the only display, driven into one mode."""
    width, height, _ = (int(value) for value in mode.split("x"))
    markers = [
        rf"^OpenRFS: display0 bound by SeaBIOS {re.escape(driver)}: ",
        rf"^ST DRV display refusal {DISPLAY_BOGUS_MODE} "
        r"the adapter has no such mode$",
        rf"^ST DRV display mode {mode} pitch [1-9][0-9]* framebuffer "
        r"0x[0-9A-Fa-f]+ mode-number 0x[0-9A-Fa-f]+ (palette|direct)$",
        rf"^ST DRV display pattern {width * height} pixels mismatches 0$",
        r"^ST DRV display captured$",
    ]
    return Scenario(plan="display", description=description, driver=driver,
                    qemu=["-vga", "none", "-device", f"{device},id=video0"],
                    markers=markers, machine=machine, timeout=timeout,
                    drivers_option=driver, mode=mode, screen="video0")


AUDIO_RATE = 44100            # must match driver_tests.c
AUDIO_FRAGMENTS = 12
AUDIO_SILENT_FRAGMENTS = 1
AUDIO_LEFT_HALF_PERIOD = 50
AUDIO_RIGHT_HALF_PERIOD = 25
AUDIO_LEFT_LEVEL = 12000
AUDIO_RIGHT_STEP = 1000


def audio_scenario(driver: str, description: str, devices: list[str],
                   timeout: int = 180) -> Scenario:
    """A sound card whose output QEMU records to a WAV file."""
    markers = [
        rf"^OpenRFS: pcm0 bound by MINIX {re.escape(driver)}: ",
        r"^ST DRV audio refusal rate 96000 format not supported by the "
        r"driver$",
        r"^ST DRV audio open pcm0 rate 44100 channels 2 bits 16 fragment "
        r"[1-9][0-9]*$",
        rf"^ST DRV audio played fragments "
        rf"{AUDIO_FRAGMENTS + AUDIO_SILENT_FRAGMENTS} interrupts "
        rf"{AUDIO_FRAGMENTS + AUDIO_SILENT_FRAGMENTS} pauses [0-9]+ "
        r"drain ok$",
    ]
    return Scenario(plan="audio", description=description, driver=driver,
                    qemu=["-audiodev", "wav,id=drvsnd,path={capture}"]
                    + devices,
                    markers=markers, timeout=timeout, drivers_option=driver)


def tpm_extend_digest() -> bytes:
    """The digest the guest extends PCR 16 with (driver_tests.c)."""
    return bytes((index * 7 + 3) & 0xFF for index in range(32))


def tpm_pcr16_expected() -> str:
    """PCR 16 after a reset and one SHA-256 extend: H(zeros || digest)."""
    return hashlib.sha256(bytes(32) + tpm_extend_digest()).hexdigest()


def tpm_scenario(driver: str, description: str, device: str,
                 machine: str = "pc") -> Scenario:
    """A TPM 2.0 emulated by swtpm behind QEMU's TIS or CRB model."""
    markers = [
        rf"^OpenRFS: tpm0 bound by SeaBIOS {re.escape(driver)}: TPM 2\.0",
        rf"^ST DRV tpm device tpm0 {re.escape(driver)} version 2$",
        r"^ST DRV tpm family 2\.0\. manufacturer \S+",
        r"^ST DRV tpm random [0-9a-f]{32} [0-9a-f]{32}$",
        r"^ST DRV tpm undefined-command rc 0x0*143$",
        # Computed here, not by the guest: SHA-256 over the reset PCR and
        # the digest the guest sent.
        rf"^ST DRV tpm pcr16 sha256 {tpm_pcr16_expected()}$",
    ]
    return Scenario(plan="tpm", description=description, driver=driver,
                    qemu=["-chardev", "socket,id=drvtpm,path={tpmsock}",
                          "-tpmdev", "emulator,id=drvtpm0,chardev=drvtpm",
                          "-device", f"{device},tpmdev=drvtpm0"],
                    markers=markers, machine=machine, drivers_option=driver)


def usb_host_marker(driver: str) -> str:
    return (rf"^OpenRFS: usb[0-9]+ bound by SeaBIOS {driver}: "
            rf"{driver} USB host controller ")


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
    # An ISA card is probed only when named: finding it means writing to
    # I/O ports nothing described. QEMU's ne2k_isa sits at 0x300, the first
    # address the driver probes.
    "net-ne2k-isa": network_scenario(
        "ne2k_isa", "ne2k-isa", "Novell NE2000 (ISA, I/O 0x300)",
        drivers_option="ne2k-isa"),
    "net-tulip": network_scenario(
        "tulip", "tulip", "DEC 21143 Tulip"),
    "net-vmxnet3": network_scenario(
        "vmxnet3", "vmxnet3", "VMware VMXNET3", machine="q35"),
    # iPXE's USB stack, named on the command line so that the SeaBIOS USB
    # drivers leave the controller alone. QEMU's usb-net offers a CDC-ECM
    # and an RNDIS configuration; the USB core chooses between them by the
    # scores of the function drivers selected, so naming one picks it.
    "net-usb-ecm-xhci": network_scenario(
        "usb-net,bus=xhci.0", "cdc-ecm",
        "USB CDC-ECM adapter on a QEMU xHCI controller",
        drivers_option="ipxe-xhci,cdc-ecm",
        mac=USB_ECM_MAC,
        nic_bus=("-device", "qemu-xhci,id=xhci")),
    "net-usb-ecm-nec-xhci": network_scenario(
        "usb-net,bus=xhci.0", "cdc-ecm",
        "USB CDC-ECM adapter on a NEC uPD720200 xHCI controller",
        drivers_option="ipxe-xhci,cdc-ecm",
        mac=USB_ECM_MAC,
        nic_bus=("-device", "nec-usb-xhci,id=xhci")),
    # QEMU 8.2's usb-net stalls an RNDIS query whose (empty) information
    # buffer starts at the end of the message: rndis_query_response()
    # rejects InformationBufferOffset + 8 >= MessageLength
    # (hw/usb/dev-network.c). iPXE's rndis.c sends every query that way
    # (offset 20, length 0, 28 bytes); Linux's rndis_host passes the check
    # only because its MAC query carries a 48-byte input buffer. The
    # initialise message before it completes, so the driver does reach the
    # model; the query for the permanent address is refused.
    "net-usb-rndis-xhci": network_scenario(
        "usb-net,bus=xhci.0", "rndis",
        "USB RNDIS adapter on a QEMU xHCI controller",
        drivers_option="ipxe-xhci,rndis",
        nic_bus=("-device", "qemu-xhci,id=xhci"),
        expected_failure="QEMU 8.2 usb-net refuses RNDIS queries with an "
                         "empty information buffer at the message end"),
    "net-usb-ecm-hub": network_scenario(
        "usb-net,bus=xhci.0,port=1.2", "cdc-ecm",
        "USB CDC-ECM adapter behind a USB hub on xHCI",
        drivers_option="ipxe-xhci,ipxe-usbhub,cdc-ecm",
        mac=USB_ECM_MAC,
        nic_bus=("-device", "qemu-xhci,id=xhci",
                 "-device", "usb-hub,bus=xhci.0,port=1")),
    "net-usb-ecm-uhci": network_scenario(
        "usb-net,bus=uhci.0", "cdc-ecm",
        "USB CDC-ECM adapter on an Intel PIIX3 UHCI controller",
        drivers_option="ipxe-uhci,cdc-ecm",
        mac=USB_ECM_MAC,
        nic_bus=("-device", "piix3-usb-uhci,id=uhci")),
    # The Intel ICH9 arrangement: an EHCI controller at 00:1d.7 with three
    # UHCI companions on the same device. usb-net is a full-speed device, so
    # iPXE's EHCI driver resets its port, finds no high-speed device and
    # hands the port to the companion, whose driver enumerates it. QEMU has
    # no high-speed network device, so no traffic crosses the EHCI schedule.
    "net-usb-ecm-ehci-companion": network_scenario(
        "usb-net,bus=ehci.0", "cdc-ecm",
        "USB CDC-ECM adapter on ICH9 EHCI, handed to its UHCI companion",
        machine="q35", drivers_option="ipxe-ehci,ipxe-uhci,cdc-ecm",
        mac=USB_ECM_MAC,
        nic_bus=("-device",
                 "ich9-usb-ehci1,id=ehci,addr=1d.7,multifunction=on",
                 "-device", "ich9-usb-uhci1,masterbus=ehci.0,firstport=0,"
                            "addr=1d.0,multifunction=on",
                 "-device", "ich9-usb-uhci2,masterbus=ehci.0,firstport=2,"
                            "addr=1d.1",
                 "-device", "ich9-usb-uhci3,masterbus=ehci.0,firstport=4,"
                            "addr=1d.2")),
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
    "blk-usb-msc-ehci": storage_scenario(
        "usb-msc", "USB mass storage (bulk-only) on an EHCI controller",
        ["-device", "usb-ehci,id=usbhc", "-drive", DISK,
         "-device", "usb-storage,bus=usbhc.0,drive=drvdisk"],
        extra_markers=(usb_host_marker("ehci"),)),
    "blk-usb-msc-uhci": storage_scenario(
        "usb-msc", "USB mass storage (bulk-only) on a PIIX3 UHCI controller",
        ["-device", "piix3-usb-uhci,id=usbhc", "-drive", DISK,
         "-device", "usb-storage,bus=usbhc.0,drive=drvdisk"],
        extra_markers=(usb_host_marker("uhci"),), timeout=300),
    "blk-usb-msc-ohci": storage_scenario(
        "usb-msc", "USB mass storage (bulk-only) on an OHCI controller",
        ["-device", "pci-ohci,id=usbhc", "-drive", DISK,
         "-device", "usb-storage,bus=usbhc.0,drive=drvdisk"],
        extra_markers=(usb_host_marker("ohci"),), timeout=300),
    # SeaBIOS's UAS driver runs UAS at high speed only ("Superspeed UAS
    # devices not supported (yet)"), so the device sits on EHCI.
    "blk-usb-uas": storage_scenario(
        "usb-uas", "USB Attached SCSI disk on an EHCI controller",
        ["-device", "usb-ehci,id=usbhc",
         "-device", "usb-uas,id=uas,bus=usbhc.0", "-drive", DISK,
         "-device", "scsi-hd,bus=uas.0,scsi-id=0,lun=0,drive=drvdisk"],
        extra_markers=(usb_host_marker("ehci"),)),
    # xHCI mass storage goes through a full-speed hub: OpenRFS's own xHCI
    # boot proof reads the first connected device and accepts only USB 2
    # root ports, and QEMU attaches usb-storage to a USB 3 port directly.
    "blk-usb-hub": storage_scenario(
        "usb-msc", "USB mass storage behind a USB 1.1 hub on xHCI",
        ["-device", "qemu-xhci,id=usbhc",
         "-device", "usb-hub,bus=usbhc.0,port=1", "-drive", DISK,
         "-device", "usb-storage,bus=usbhc.0,port=1.1,drive=drvdisk"],
        extra_markers=(usb_host_marker("xhci"),), timeout=300),
    # The keyboard sits on root port 1 as a USB 2 device: exactly the
    # fixture OpenRFS's own xHCI boot proof reads before the upstream
    # driver takes the controller.
    "hid-kbd-xhci": hid_scenario(
        "kbd", "USB HID keyboard on an xHCI controller",
        ["-device", "qemu-xhci,id=usbhc",
         "-device", "usb-kbd,id=usbkbd,bus=usbhc.0,port=1,usb_version=2"],
        host="xhci"),
    "hid-kbd-uhci": hid_scenario(
        "kbd", "USB HID keyboard on a PIIX3 UHCI controller",
        ["-device", "piix3-usb-uhci,id=usbhc",
         "-device", "usb-kbd,id=usbkbd,bus=usbhc.0"],
        host="uhci"),
    "hid-mouse-ohci": hid_scenario(
        "mouse", "USB HID mouse on an OHCI controller",
        ["-device", "pci-ohci,id=usbhc",
         "-device", "usb-mouse,id=usbmouse,bus=usbhc.0"],
        host="ohci"),
    # ICH9 EHCI with its three UHCI companions, bound in SeaBIOS's order
    # (EHCI first). QEMU's EHCI model drives these full-speed devices on
    # its own ports rather than handing them to a companion, so the
    # companions bind, find nothing and are released.
    "hid-ich9-ehci": hid_scenario(
        "both", "ICH9 EHCI (UHCI companions present): keyboard and mouse",
        ["-device", "ich9-usb-ehci1,id=usbhc,addr=1d.7,multifunction=on",
         "-device", "ich9-usb-uhci1,masterbus=usbhc.0,firstport=0,"
                    "addr=1d.0,multifunction=on",
         "-device", "ich9-usb-uhci2,masterbus=usbhc.0,firstport=2,"
                    "addr=1d.1,multifunction=on",
         "-device", "ich9-usb-uhci3,masterbus=usbhc.0,firstport=4,"
                    "addr=1d.2,multifunction=on",
         "-device", "usb-kbd,id=usbkbd,bus=usbhc.0,port=1",
         "-device", "usb-mouse,id=usbmouse,bus=usbhc.0,port=2"],
        host="ehci", machine="q35"),
    # The standard VGA and Cirrus builds program an adapter that no option
    # ROM has touched: with romfile= empty the firmware has no VGA BIOS to
    # run, GRUB sets no mode (so OpenRFS boots on serial alone), and the
    # driver's own setup brings the card up from reset. This is also the
    # only way to boot OpenRFS on a Cirrus card: its VGA BIOS offers no
    # 32-bit mode, and OpenRFS refuses any other Multiboot2 framebuffer.
    # With a ROM, the standard VGA would be left in a Bochs VBE mode, which
    # QEMU's VGA core keeps overriding the standard CRTC registers with.
    "display-stdvga": display_scenario(
        "stdvga", "QEMU standard VGA from reset, VGA mode 13h (320x200x8)",
        "VGA,romfile=", "320x200x8"),
    "display-bochsvga": display_scenario(
        "bochsvga", "QEMU standard VGA (Bochs VBE DISPI), 800x600 32 bpp",
        "VGA", "800x600x32"),
    "display-bochsvga-8bpp": display_scenario(
        "bochsvga", "QEMU standard VGA (Bochs VBE DISPI), 1024x768 8 bpp",
        "VGA", "1024x768x8"),
    "display-bochsvga-vmware": display_scenario(
        "bochsvga", "VMware SVGA II through its VBE DISPI interface",
        "vmware-svga", "800x600x32"),
    "display-bochsvga-qxl": display_scenario(
        "bochsvga", "QXL VGA through its VBE DISPI interface",
        "qxl-vga", "800x600x32"),
    "display-bochsvga-virtio": display_scenario(
        "bochsvga", "virtio-vga through its VBE DISPI interface",
        "virtio-vga", "800x600x32"),
    "display-cirrus": display_scenario(
        "cirrus", "Cirrus Logic CL-GD5446 from reset, 800x600 24 bpp",
        "cirrus-vga,romfile=", "800x600x24"),
    "display-cirrus-16bpp": display_scenario(
        "cirrus", "Cirrus Logic CL-GD5446 from reset, 1024x768 16 bpp",
        "cirrus-vga,romfile=", "1024x768x16"),
    "display-ati-rv100": display_scenario(
        "ati", "ATI Radeon 7000 (RV100), 800x600 32 bpp",
        "ati-vga,model=rv100", "800x600x32"),
    "display-ati-rage128": display_scenario(
        "ati", "ATI Rage 128 Pro, 640x480 16 bpp",
        "ati-vga,model=rage128p", "640x480x16"),
    "display-bochs-display": display_scenario(
        "bochs-display", "QEMU bochs-display (no legacy VGA), 1024x768",
        "bochs-display,xres=1024,yres=768", "1024x768x32"),
    "display-ramfb": display_scenario(
        "ramfb", "QEMU ramfb (fw_cfg RAM framebuffer), 1024x768",
        "ramfb", "1024x768x32"),
    "audio-es1370": audio_scenario(
        "es1370", "Ensoniq AudioPCI ES1370 (MINIX 3 driver)",
        ["-device", "ES1370,audiodev=drvsnd"]),
    # MINIX's driver is built for IRQ 7, base 0x220 and DMA 1/5; the
    # parallel port that would share IRQ 7 is removed.
    "audio-sb16": audio_scenario(
        "sb16", "Creative Sound Blaster 16 (ISA, MINIX 3 driver)",
        ["-device", "sb16,audiodev=drvsnd,iobase=0x220,irq=7,dma=1,dma16=5",
         "-parallel", "none"]),
    "tpm-tis": tpm_scenario(
        "tpm-tis", "TPM 2.0 (swtpm) behind the TIS/FIFO interface", "tpm-tis"),
    "tpm-crb": tpm_scenario(
        "tpm-crb", "TPM 2.0 (swtpm) behind the CRB interface", "tpm-crb",
        machine="q35"),
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
                 *scenario.nic_bus,
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
    capture = work / "capture.wav"
    if capture.exists():
        capture.unlink()
    swtpm = None
    tpm_dir = None
    tpm_socket = ""
    if scenario.plan == "tpm":
        # A fresh TPM 2.0 per boot; the socket path must fit in 108 bytes.
        tpm_dir = Path(tempfile.mkdtemp(prefix="orfs-tpm-"))
        tpm_socket = str(tpm_dir / "ctrl.sock")
        swtpm = subprocess.Popen(
            ["swtpm", "socket", "--tpm2", "--tpmstate",
             f"dir={tpm_dir}", "--ctrl", f"type=unixio,path={tpm_socket}",
             "--log", f"file={work / 'swtpm.log'},level=1"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            if Path(tpm_socket).exists():
                break
            time.sleep(0.05)
    qemu += [argument.replace("{image}", str(image))
             .replace("{capture}", str(capture))
             .replace("{tpmsock}", tpm_socket)
             for argument in scenario.qemu]
    injector = None
    screen = work / "screen.ppm"
    if screen.exists():
        screen.unlink()
    if scenario.plan == "display":
        options += [f"openrfs.drvmode={scenario.mode}"]
        qmp = Path(tempfile.mkdtemp(prefix="orfs-qmp-")) / "qmp.sock"
        qemu += ["-qmp", f"unix:{qmp},server=on,wait=off"]
        injector = threading.Thread(
            target=capture_screen, args=(log, qmp, scenario, screen, work),
            daemon=True)
    if scenario.plan == "hid":
        options += [f"openrfs.drvkind={scenario.kind}",
                    f"openrfs.drvtext={scenario.text}"]
        # A Unix socket path must fit in 108 bytes; the work tree may not.
        qmp = Path(tempfile.mkdtemp(prefix="orfs-qmp-")) / "qmp.sock"
        qemu += ["-qmp", f"unix:{qmp},server=on,wait=off"]
        injector = threading.Thread(
            target=inject_input, args=(log, qmp, scenario), daemon=True)
    iso = build_iso(args.kernel, work, " ".join(options), args.grub_mkrescue)
    qemu += ["-cdrom", str(iso)] + args.qemu_arg
    if log.exists():
        log.unlink()
    try:
        process = subprocess.Popen(qemu, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE)
        if injector is not None:
            injector.start()
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
        if swtpm is not None:
            swtpm.terminate()
            try:
                swtpm.wait(timeout=10)
            except subprocess.TimeoutExpired:
                swtpm.kill()
        if tpm_dir is not None:
            shutil.rmtree(tpm_dir, ignore_errors=True)
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
    if scenario.plan == "display" and not problems:
        problems += check_screen(screen, scenario, work)
    if scenario.plan == "audio" and not problems:
        fragment_bytes = 0
        for line in lines:
            match = re.match(r"^ST DRV audio open \S+ .* fragment ([0-9]+)$",
                             line)
            if match:
                fragment_bytes = int(match.group(1))
        problems += check_capture(capture, work, fragment_bytes // 4)
    if scenario.plan == "hid" and scenario.kind in ("kbd", "both"):
        # A make and a break byte per key, except that the guest stops
        # reading at the last key's press.
        for line in lines:
            match = re.match(r"^ST DRV hid keys \S+ usb-bytes ([0-9]+) ", line)
            if match and int(match.group(1)) < 2 * len(scenario.text) - 1:
                problems.append("fewer USB scancode bytes than keystrokes")
    if injector is not None:
        shutil.rmtree(qmp.parent, ignore_errors=True)
    evidence = [line for line in lines if line.startswith("ST DRV")]
    (work / "evidence.txt").write_text("\n".join(evidence) + "\n")
    return not problems, "; ".join(problems)


def inject_input(log: Path, qmp: Path, scenario: Scenario) -> None:
    """Type and move through QMP once the guest reports it is ready."""
    deadline = time.monotonic() + scenario.timeout
    while time.monotonic() < deadline:
        if log.exists() and "ST DRV hid ready" in log.read_text(
                errors="replace"):
            break
        time.sleep(0.2)
    else:
        return
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(str(qmp))
        stream = sock.makefile("rw")

        def command(name: str, arguments: dict | None = None) -> None:
            message = {"execute": name}
            if arguments is not None:
                message["arguments"] = arguments
            stream.write(json.dumps(message) + "\n")
            stream.flush()
            while True:
                reply = json.loads(stream.readline())
                if "return" in reply or "error" in reply:
                    return

        stream.readline()  # greeting
        command("qmp_capabilities")
        if scenario.kind in ("kbd", "both"):
            for index, character in enumerate(scenario.text):
                for down in (True, False):
                    # A keyboard-only guest exits on the final press. There
                    # is no receiver left for its release event.
                    if not down and scenario.kind == "kbd" and \
                            index + 1 == len(scenario.text):
                        break
                    command("input-send-event", {"events": [
                        {"type": "key", "data": {
                            "down": down,
                            "key": {"type": "qcode", "data": character}}}]})
                    time.sleep(0.08)
        if scenario.kind in ("mouse", "both"):
            for _ in range(4):
                command("input-send-event", {"events": [
                    {"type": "rel", "data": {"axis": "x", "value": 10}},
                    {"type": "rel", "data": {"axis": "y", "value": 5}}]})
                time.sleep(0.08)
            for down in (True, False):
                command("input-send-event", {"events": [
                    {"type": "btn", "data": {"down": down,
                                             "button": "left"}}]})
                time.sleep(0.08)


def capture_screen(log: Path, qmp: Path, scenario: Scenario, screen: Path,
                   work: Path) -> None:
    """Once the guest has drawn, dump the screen and acknowledge it."""
    deadline = time.monotonic() + scenario.timeout
    while time.monotonic() < deadline:
        if log.exists() and "ST DRV display ready" in log.read_text(
                errors="replace"):
            break
        time.sleep(0.2)
    else:
        return
    errors = []
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(str(qmp))
        stream = sock.makefile("rw")

        def command(name: str, arguments: dict | None = None) -> None:
            message = {"execute": name}
            if arguments is not None:
                message["arguments"] = arguments
            stream.write(json.dumps(message) + "\n")
            stream.flush()
            while True:
                reply = json.loads(stream.readline())
                if "error" in reply:
                    errors.append(f"{name}: {reply['error']}")
                    return
                if "return" in reply:
                    return

        stream.readline()  # greeting
        command("qmp_capabilities")
        arguments = {"filename": str(screen)}
        if scenario.screen:
            arguments["device"] = scenario.screen
        command("screendump", arguments)
        # The display guest exits on this press; a release races its QMP
        # socket shutdown and does not contribute to the screenshot proof.
        command("input-send-event", {"events": [
            {"type": "key", "data": {
                "down": True, "key": {"type": "qcode", "data": "ret"}}}]})
    if errors:
        (work / "qmp-errors.txt").write_text("\n".join(errors) + "\n")


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    """A binary PPM (P6, maxval 255) as width, height and RGB bytes."""
    data = path.read_bytes()
    fields: list[bytes] = []
    offset = 0
    while len(fields) < 4:
        while data[offset:offset + 1].isspace():
            offset += 1
        if data[offset:offset + 1] == b"#":
            offset = data.index(b"\n", offset) + 1
            continue
        end = offset
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[offset:end])
        offset = end
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise ValueError("not a P6 PPM with maxval 255")
    width, height = int(fields[1]), int(fields[2])
    pixels = data[offset + 1:offset + 1 + width * height * 3]
    if len(pixels) != width * height * 3:
        raise ValueError("truncated PPM")
    return width, height, pixels


# The guest's quadrants: red, green, blue, white (see driver_tests.c).
QUADRANT_COLORS = ((1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 1))


def check_screen(screen: Path, scenario: Scenario, work: Path) -> list[str]:
    """Check what QEMU's display model scanned out of the adapter."""
    if not screen.exists():
        errors = work / "qmp-errors.txt"
        detail = errors.read_text().strip() if errors.exists() else ""
        return [f"no screendump was captured {detail}".strip()]
    try:
        width, height, pixels = read_ppm(screen)
    except (ValueError, IndexError) as error:
        return [f"unreadable screendump: {error}"]
    mode_width, mode_height, _ = (int(v) for v in scenario.mode.split("x"))
    if width % mode_width or height % mode_height:
        return [f"screen is {width}x{height}, not the mode "
                f"{mode_width}x{mode_height} or a whole multiple of it"]
    scale_x, scale_y = width // mode_width, height // mode_height
    if scale_x not in (1, 2) or scale_y not in (1, 2):
        return [f"screen is {width}x{height} for a "
                f"{mode_width}x{mode_height} mode"]

    def colour(x: int, y: int) -> tuple[int | None, ...]:
        at = ((y * scale_y) * width + x * scale_x) * 3
        return tuple(1 if value >= 0x80 else 0 if value <= 0x40 else None
                     for value in pixels[at:at + 3])

    def expected(x: int, y: int) -> tuple[int, int, int]:
        return QUADRANT_COLORS[(2 if y >= mode_height // 2 else 0) +
                               (1 if x >= mode_width // 2 else 0)]

    points = set()
    step_x, step_y = max(1, mode_width // 32), max(1, mode_height // 32)
    for y in range(0, mode_height, step_y):
        for x in range(0, mode_width, step_x):
            points.add((x, y))
    # The quadrant edges fall exactly on the middle row and column on every
    # line: a wrong pitch would shear them.
    for y in range(0, mode_height, step_y):
        points.update({(mode_width // 2 - 1, y), (mode_width // 2, y)})
    for x in range(0, mode_width, step_x):
        points.update({(x, mode_height // 2 - 1), (x, mode_height // 2)})
    points.update({(0, 0), (mode_width - 1, 0), (0, mode_height - 1),
                   (mode_width - 1, mode_height - 1)})
    wrong = [(x, y) for x, y in sorted(points) if colour(x, y) != expected(x, y)]
    (work / "screen-check.txt").write_text(
        f"screen {width}x{height} mode {scenario.mode} "
        f"samples {len(points)} wrong {len(wrong)}\n")
    if wrong:
        x, y = wrong[0]
        return [f"{len(wrong)} of {len(points)} sampled pixels wrong, "
                f"first at {x},{y}: {colour(x, y)} for {expected(x, y)}"]
    return []


def read_wav(path: Path) -> tuple[int, int, int, bytes]:
    """A PCM WAV file as rate, channels, bits and its sample bytes.

    QEMU writes the RIFF sizes when it shuts its audio down; a guest that
    leaves through the debug-exit device can leave them zero, so the data
    runs to the end of the file."""
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a RIFF WAVE file")
    offset = 12
    rate = channels = bits = 0
    while offset + 8 <= len(data):
        chunk = data[offset:offset + 4]
        size = int.from_bytes(data[offset + 4:offset + 8], "little")
        body = offset + 8
        if chunk == b"fmt ":
            if int.from_bytes(data[body:body + 2], "little") != 1:
                raise ValueError("not integer PCM")
            channels = int.from_bytes(data[body + 2:body + 4], "little")
            rate = int.from_bytes(data[body + 4:body + 8], "little")
            bits = int.from_bytes(data[body + 14:body + 16], "little")
        elif chunk == b"data":
            return rate, channels, bits, data[body:]
        offset = body + size + (size & 1)
    raise ValueError("no data chunk")


def check_capture(capture: Path, work: Path,
                  per_fragment: int) -> list[str]:
    """Check what QEMU's model of the card played, sample by sample."""
    if not capture.exists():
        return ["QEMU recorded no audio"]
    try:
        rate, channels, bits, samples = read_wav(capture)
    except (ValueError, IndexError) as error:
        return [f"unreadable capture: {error}"]
    if (rate, channels, bits) != (AUDIO_RATE, 2, 16):
        return [f"capture is {rate} Hz, {channels} channels, {bits} bits"]
    frames = [(int.from_bytes(samples[at:at + 2], "little", signed=True),
               int.from_bytes(samples[at + 2:at + 4], "little", signed=True))
              for at in range(0, len(samples) - 3, 4)]
    loud = [index for index, (left, _) in enumerate(frames)
            if abs(left) > AUDIO_LEFT_LEVEL // 4]
    if not loud:
        return ["the capture is silent"]
    tone = frames[loud[0]:loud[-1] + 1]
    report = [f"capture frames {len(frames)} tone frames {len(tone)} "
              f"first at {loud[0]}"]
    problems = []
    if per_fragment == 0 or len(tone) != AUDIO_FRAGMENTS * per_fragment:
        problems.append(f"{len(tone)} tone frames is not {AUDIO_FRAGMENTS} "
                        f"fragments of {per_fragment} frames")
    else:
        # Scale by what the card's path did to the left channel's level.
        levels = sorted(abs(left) for left, _ in tone)
        scale = levels[len(levels) // 2] / AUDIO_LEFT_LEVEL
        wrong = 0
        first = None
        for n, (left, right) in enumerate(tone):
            fragment = n // per_fragment
            want_left = (AUDIO_LEFT_LEVEL if (n // AUDIO_LEFT_HALF_PERIOD)
                         % 2 == 0 else -AUDIO_LEFT_LEVEL) * scale
            level = AUDIO_RIGHT_STEP * (fragment + 1) * scale
            want_right = (level if (n // AUDIO_RIGHT_HALF_PERIOD) % 2 == 0
                          else -level)
            if (abs(left - want_left) > 0.02 * AUDIO_LEFT_LEVEL + 4 or
                    abs(right - want_right) > 0.02 * level + 4):
                wrong += 1
                if first is None:
                    first = (n, left, right, round(want_left),
                             round(want_right))
        report.append(f"fragment frames {per_fragment} scale {scale:.4f} "
                      f"wrong {wrong}")
        if wrong:
            problems.append(f"{wrong} of {len(tone)} frames differ from the "
                            f"signal; first (frame, left, right, expected "
                            f"left, expected right) {first}")
    (work / "capture-check.txt").write_text("\n".join(report) + "\n")
    return problems


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
