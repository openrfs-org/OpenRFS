<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Upstream drivers

## Status of the port to current main

This branch is a draft integration. The 42-driver count below describes the
source branch's QEMU model evidence, not 42 drivers available to every
production consumer on current main. The iPXE NIC registry feeds the normal
network stack, and SeaBIOS USB HID feeds the normal keyboard and pointer
queues. The storage, display, PCM, and TPM scenarios exercise the real
vendored drivers through their registries, but current production FAT32/ext4
and VFS still use NVMe sessions, the active UI still uses the loader
framebuffer, native audio still uses HD Audio, and no non-test platform
service calls the TPM registry. Those connections and their end-to-end tests
are required before this port can be merged.

The bind and removal review is also open: iPXE and SeaBIOS currently share
one DMA arena across devices in a layer, and failure and teardown paths need
transactional registry rollback and device quiescence before claims or DMA
memory are released. The named `ramfb` fw_cfg wait has no timeout. No
physical hardware has been tested.

OpenRFS runs hardware drivers taken unmodified from three long-lived open
source projects:

- **iPXE**, the network boot firmware: network adapters and its USB stack;
- **SeaBIOS**, QEMU's default x86 BIOS and a common coreboot payload:
  storage controllers, its USB stack, VGA adapters and TPM interfaces;
- **MINIX 3**: sound cards.

Every vendored file is byte-for-byte upstream, pinned to one commit and
recorded with its digest (`vendor/<project>/UPSTREAM-COMMIT.txt`,
`SOURCE-MANIFEST.sha256`, checked by `make driver-provenance`). OpenRFS never
patches them. Each project's drivers compile against compatibility headers
that recreate the environment they were written for (`ports/<project>/`),
and a kernel-side host connects that environment to the upstream driver
framework (`src/kernel/hwdrv.c`): PCI claims, bus mastering limited to a
per-layer DMA arena below 4 GiB, and the netdev, block, display, PCM and TPM
registries. How each layer does it, and where it differs from upstream's own
runtime, is in `vendor/<project>/OPENRFS-PORT.md`.

## Selecting drivers

`openrfs.drivers=` on the kernel command line decides what binds:

- `auto`: every driver whose device PCI enumeration finds, plus the floppy
  controller, which SeaBIOS's driver finds from the CMOS drive types. The
  exceptions bind only by name: SeaBIOS's NVMe driver (OpenRFS's own NVMe
  driver owns those controllers), the iPXE USB stack (the SeaBIOS USB drivers
  own the controllers under `auto`, and two stacks must never share one) and
  the VGA drivers (they reprogram the display the console is using);
- `name,name,...`: only the named drivers. ISA cards (NE2000, Sound Blaster
  16) and the TPM at its fixed address also bind only this way, because
  probing them writes to addresses nothing described.

SeaBIOS's USB class drivers (hub, HID, mass storage, UAS) are not selected
separately: they attach behind whichever SeaBIOS controller driver binds,
and their names appear in the binding records. iPXE's USB function drivers
(`ipxe-usbhub`, `cdc-ecm`, `rndis`) are selected by name alongside a
controller driver.

The layers bind in the order SeaBIOS, MINIX, iPXE, SeaBIOS VGA, and a PCI
function claimed by one layer is invisible to the next.

## Evidence, and what it does not show

All evidence comes from QEMU 8.2.2 (TCG) device models, driven by
`tools/run_driver_tests.py` (`make qemu-test-drivers`). **No driver here has
been run on physical hardware by this work.** QEMU models the register
interfaces of the real parts, and a pass is evidence about those models; it
is not a hardware qualification. Each scenario boots OpenRFS and requires the
driver to do real work, checked from outside the guest where possible:

| Plan | What a pass requires |
| --- | --- |
| net | a DHCP lease from QEMU's user network, ICMP echo replies, and a 256 KiB HTTP download verified byte for byte (packet capture kept) |
| blk | reads verified at the start, middle and end of a fixture image, an out-of-range request refused, and on writable media a rewrite read back in the guest and checked in the image on the host afterwards |
| hid | keystrokes and mouse motion injected through QEMU's monitor arrive in the guest |
| display | the guest sets the mode, draws a quadrant pattern and reads every pixel back; the host takes a screendump and checks each quadrant's colour and edges |
| audio | 12 tone fragments and one silent fragment; the host records QEMU's WAV output and requires all 98,304 signal frames to match what the guest generated |
| tpm | TPM2 commands to swtpm; the PCR 16 value after an extend must equal the value the host computes |

## The matrix

"Verified" means every scenario listed passes. Driver names are the ones
`openrfs.drivers=` takes.

### Network: iPXE

| # | Driver | Upstream source | Licence | Binds | QEMU scenarios | Status |
| ---: | --- | --- | --- | --- | --- | --- |
| 1 | `intel` | `src/drivers/net/intel.c` | GPL-2.0-or-later OR UBDL | auto | `net-e1000` (82540EM), `net-e1000-82544gc`, `net-e1000-82545em`, `net-e1000e` (82574L); `net-igb` (82576) expected failure | verified |
| 2 | `eepro100` | `src/drivers/net/eepro100.c` | GPL-2.0-or-later | auto | `net-i82557b`, `net-i82559er`, `net-i82550` | verified |
| 3 | `realtek` | `src/drivers/net/realtek.c` | GPL-2.0-or-later OR UBDL | auto | `net-rtl8139` (RTL8139C+; QEMU has no RTL8169 model) | verified (RTL8139) |
| 4 | `pcnet32` | `src/drivers/net/pcnet32.c` | GPL-2.0-or-later | auto | `net-pcnet` (AMD Am79C970A) | verified |
| 5 | `vmxnet3` | `src/drivers/net/vmxnet3.c` | GPL-2.0-or-later OR UBDL | auto | `net-vmxnet3` | verified |
| 6 | `tulip` | `src/drivers/net/tulip.c` | GPL-1.0-or-later | auto | `net-tulip` (DEC 21143) | verified |
| 7 | `ne2k-pci` | `src/drivers/net/ns8390.c` | BSD-2-Clause | auto | `net-ne2k-pci` (RTL8029) | verified |
| 8 | `ne2k-isa` | `src/drivers/net/ne2k_isa.c` | BSD-2-Clause | named | `net-ne2k-isa` (NE2000 at 0x300) | verified |

### USB: iPXE

| # | Driver | Upstream source | Licence | Binds | QEMU scenarios | Status |
| ---: | --- | --- | --- | --- | --- | --- |
| 9 | `ipxe-xhci` | `src/drivers/usb/xhci.c` | GPL-2.0-or-later OR UBDL | named | `net-usb-ecm-xhci` (qemu-xhci), `net-usb-ecm-nec-xhci` (NEC uPD720200), `net-usb-ecm-hub` | verified |
| 10 | `ipxe-uhci` | `src/drivers/usb/uhci.c` | GPL-2.0-or-later OR UBDL | named | `net-usb-ecm-uhci` (PIIX3), `net-usb-ecm-ehci-companion` (ICH9 companions) | verified |
| 11 | `ipxe-usbhub` | `src/drivers/usb/usbhub.c` | GPL-2.0-or-later OR UBDL | named | `net-usb-ecm-hub` (adapter behind a USB 1.1 hub) | verified |
| 12 | `cdc-ecm` | `src/drivers/net/ecm.c` | GPL-2.0-or-later OR UBDL | named | the five scenarios above | verified |
| - | `ipxe-ehci` | `src/drivers/usb/ehci.c` | GPL-2.0-or-later OR UBDL | named | `net-usb-ecm-ehci-companion` | partial: controller start-up, port reset and hand-off to the UHCI companion; QEMU has no high-speed network device, so no traffic crosses the EHCI schedules |
| - | `rndis` | `src/drivers/net/acm.c`, `src/net/rndis.c` | GPL-2.0-or-later OR UBDL | named | `net-usb-rndis-xhci` | expected failure: QEMU 8.2's usb-net stalls iPXE's RNDIS queries (see `vendor/ipxe/OPENRFS-PORT.md`) |

### Storage: SeaBIOS

| # | Driver | Upstream source | Licence | Binds | QEMU scenarios | Status |
| ---: | --- | --- | --- | --- | --- | --- |
| 13 | `ahci` | `src/hw/ahci.c` | LGPL-3.0 | auto | `blk-ahci`, `blk-ahci-cd` (ICH9) | verified |
| 14 | `ata` | `src/hw/ata.c` | LGPL-3.0 | auto | `blk-ata`, `blk-ata-cd` (PIIX3 IDE) | verified |
| 15 | `virtio-blk` | `src/hw/virtio-blk.c` | LGPL-3.0 | auto | `blk-virtio-blk`, `-legacy`, `-modern` | verified |
| 16 | `virtio-scsi` | `src/hw/virtio-scsi.c` | LGPL-3.0 | auto | `blk-virtio-scsi`, `blk-virtio-scsi-cd` | verified |
| 17 | `lsi-scsi` | `src/hw/lsi-scsi.c` | LGPL-3.0 | auto | `blk-lsi` (LSI 53C895A) | verified |
| 18 | `esp-scsi` | `src/hw/esp-scsi.c` | LGPL-3.0 | auto | `blk-esp` (AMD Am53C974), `blk-dc390` | verified |
| 19 | `megasas` | `src/hw/megasas.c` | LGPL-3.0 | auto | `blk-megasas` (SAS 1078), `blk-megasas-gen2` (SAS 2108) | verified |
| 20 | `mpt-scsi` | `src/hw/mpt-scsi.c` | LGPL-3.0 | auto | `blk-mptsas` (SAS1068) | verified |
| 21 | `pvscsi` | `src/hw/pvscsi.c` | LGPL-3.0 | auto | `blk-pvscsi` | verified |
| 22 | `sdcard` | `src/hw/sdcard.c` | LGPL-3.0 | auto | `blk-sdhci` (SDHCI 3.0) | verified |
| 23 | `nvme` | `src/hw/nvme.c` | LGPL-3.0 | named | `blk-nvme` | verified |
| 24 | `floppy` | `src/hw/floppy.c` | LGPL-3.0 | auto | `blk-floppy` (82077AA, 1.44 MB) | verified |

### USB: SeaBIOS

| # | Driver | Upstream source | Licence | Binds | QEMU scenarios | Status |
| ---: | --- | --- | --- | --- | --- | --- |
| 25 | `xhci` | `src/hw/usb-xhci.c` | LGPL-3.0 | auto | `hid-kbd-xhci`, `blk-usb-hub` | verified |
| 26 | `ehci` | `src/hw/usb-ehci.c` | LGPL-3.0 | auto | `blk-usb-msc-ehci`, `blk-usb-uas`, `hid-ich9-ehci` | verified |
| 27 | `uhci` | `src/hw/usb-uhci.c` | LGPL-3.0 | auto | `blk-usb-msc-uhci`, `hid-kbd-uhci`, `hid-ich9-ehci` | verified |
| 28 | `ohci` | `src/hw/usb-ohci.c` | LGPL-3.0 | auto | `blk-usb-msc-ohci`, `hid-mouse-ohci` | verified |
| 29 | `usb-hub` | `src/hw/usb-hub.c` | LGPL-3.0 | with its controller | `blk-usb-hub` | verified |
| 30 | `usb-hid` | `src/hw/usb-hid.c` | LGPL-3.0 | with its controller | `hid-kbd-xhci`, `hid-kbd-uhci`, `hid-mouse-ohci`, `hid-ich9-ehci` | verified |
| 31 | `usb-msc` | `src/hw/usb-msc.c` | LGPL-3.0 | with its controller | `blk-usb-msc-ehci`, `-uhci`, `-ohci`, `blk-usb-hub` | verified |
| 32 | `usb-uas` | `src/hw/usb-uas.c` | LGPL-3.0 | with its controller | `blk-usb-uas` | verified |

### Display: SeaBIOS VGA

| # | Driver | Upstream source | Licence | Binds | QEMU scenarios | Status |
| ---: | --- | --- | --- | --- | --- | --- |
| 33 | `stdvga` | `vgasrc/stdvga.c` | LGPL-3.0 | named | `display-stdvga` (mode 13h from reset) | verified |
| 34 | `bochsvga` | `vgasrc/bochsvga.c` | LGPL-3.0 | named | `display-bochsvga`, `-8bpp`, `-vmware`, `-qxl`, `-virtio` | verified |
| 35 | `cirrus` | `vgasrc/clext.c` | LGPL-3.0 | named | `display-cirrus`, `display-cirrus-16bpp` (CL-GD5446) | verified |
| 36 | `ati` | `vgasrc/atiext.c` | LGPL-3.0 | named | `display-ati-rv100` (Radeon 7000), `display-ati-rage128` | verified |
| 37 | `bochs-display` | `vgasrc/bochsdisplay.c` | LGPL-3.0 | named | `display-bochs-display` | verified |
| 38 | `ramfb` | `vgasrc/ramfb.c` | LGPL-3.0 | named | `display-ramfb` | verified |

### Audio: MINIX 3

| # | Driver | Upstream source | Licence | Binds | QEMU scenarios | Status |
| ---: | --- | --- | --- | --- | --- | --- |
| 39 | `es1370` | `minix/drivers/audio/es1370/` | MINIX 3 (BSD-3-Clause) | auto | `audio-es1370` | verified |
| 40 | `sb16` | `minix/drivers/audio/sb16/` | MINIX 3 (BSD-3-Clause) | named | `audio-sb16` (0x220, IRQ 7, DMA 1/5) | verified |

### Platform: SeaBIOS TPM

| # | Driver | Upstream source | Licence | Binds | QEMU scenarios | Status |
| ---: | --- | --- | --- | --- | --- | --- |
| 41 | `tpm-tis` | `src/hw/tpm_drivers.c` | LGPL-3.0 | named | `tpm-tis` (swtpm, TPM 2.0) | verified |
| 42 | `tpm-crb` | `src/hw/tpm_drivers.c` | LGPL-3.0 | named | `tpm-crb` (swtpm, TPM 2.0) | verified |

That is 42 drivers verified against QEMU models, one partially exercised
(`ipxe-ehci`) and one whose only available model cannot carry it (`rndis`).
Four of the iPXE USB drivers drive the same kinds of controller as SeaBIOS
drivers do; they are separate implementations, and the command line chooses
between the stacks.

## What was left out, and why

- **GPL-2.0-only code**, which cannot be combined with OpenRFS's
  GPL-3.0-only licence. That excludes every Linux driver, including the AMD
  and Intel graphics drivers, and iPXE's GPL-2.0-only drivers (for example
  tg3, bnxt, sky2).
- **iPXE's virtio-net.** The implementation with years of use depends on
  `virtio-pci.c`, which carries no licence declaration; its relicensed
  replacement is in no iPXE release yet. OpenRFS has its own virtio-net
  driver.
- **Planar VGA modes.** The display registry takes packed and direct-colour
  modes only.
