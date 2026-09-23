# OpenRFS iPXE driver boundary

The files in this directory are byte-for-byte iPXE sources (see
`UPSTREAM-COMMIT.txt` and `SOURCE-MANIFEST.sha256`). OpenRFS does not patch
them. They are compiled freestanding with `-nostdinc`, MMX/SSE disabled, no
red zone, and `ports/ipxe/include/compiler.h` force-included exactly as iPXE's
own build force-includes its `compiler.h`.

## What OpenRFS provides instead of iPXE's runtime

`ports/ipxe/include/` supplies the headers the drivers include - PCI, DMA,
I/O, timer, malloc, net device and legacy NIC interfaces - with iPXE's names
and contracts. `ports/ipxe/ipxe_glue.c` implements them and
`src/kernel/ipxe_host.c` connects them to the kernel:

* **PCI.** A driver's device is claimed through the upstream driver framework
  (`src/kernel/hwdrv.c`) before its probe runs. Configuration writes that
  would change decode, bus mastering, BARs or the expansion ROM are refused
  or routed through the claim; memory decode follows a BAR mapping and I/O
  decode follows an explicit request.
* **DMA.** Every allocation - descriptor rings, packet buffers, the driver's
  private structures - is carved from one identity-mapped DMA arena below
  4 GiB that is transferred to device ownership before any iPXE device may
  master the bus. `dma_map()` refuses any buffer outside that arena.
* **Net devices.** The net_device queueing, completion, link and open/close
  semantics of iPXE's `net/netdevice.c` are reproduced; received frames go to
  OpenRFS's IPv4 stack through `src/kernel/netdev.c` instead of to iPXE's
  protocols. The receive queue is bounded (64 frames) because the kernel
  drains it from its own service loop.
* **Time and output.** `udelay()`/`mdelay()` busy-wait on the monotonic clock;
  `currticks()` counts milliseconds (`TICKS_PER_SEC` 1000). `printf()` goes to
  the serial console. Debug and profiling macros compile away, as they do in
  every non-debug iPXE build.

All drivers run polled. OpenRFS does not route PCI INTx interrupts, and iPXE's
drivers are written to be polled.

* **ISA.** `ne2k_isa.c` is a legacy ISA driver: `ISA_DRIVER()` lists the
  I/O addresses it probes and its `ne_probe1()` check, and iPXE's isa.c
  tries each address in turn. The glue does the same for a driver the
  command line names (`openrfs.drivers=ne2k-isa`), never under `auto`,
  because probing writes to I/O ports nothing described. `ne_probe1()` as
  upstream writes its arguments to `outb()` in the wrong order, which sends
  the low byte of the probe address to port 0x61 (the PC speaker gate); on
  a PC that write is harmless, and the file is kept as upstream has it.

## USB

iPXE's USB stack is built the way iPXE builds it, from its own pieces:
`drivers/bus/usb.c` (the USB core), the xHCI, EHCI and UHCI host controller
drivers, the hub driver, `usbnet.c`, the CDC-ECM driver (`ecm.c`) and the
RNDIS driver (`acm.c` with `net/rndis.c`), plus iPXE's scheduler
(`core/process.c`) and linker-table machinery (`include/ipxe/tables.h`).

* **Linker tables.** The USB core finds function drivers in a linker table
  and starts its permanent process from another. The USB objects are linked
  by `ld -r` with `ports/ipxe/usb-layer.ld`, which gathers every `.tbl.*`
  section with `KEEP(*(SORT(.tbl.*)))` exactly as iPXE's
  `arch/x86/scripts/pcbios.lds` does, into one `.data.ipxe_usb_tables`
  section the kernel's `linker.ld` places with `.data`. objcopy then makes
  every symbol local except the entry points in `ports/ipxe/usb-exports.txt`.
* **Initialisation and scheduling.** `ports/ipxe/usb_glue.c` runs iPXE's
  `initialise()` pass over `INIT_FNS` once, which starts the USB core's
  permanent process, and one `step()` of iPXE's scheduler each time the
  kernel services a USB network interface, as iPXE's main loop runs one
  between network polls. Bus polling, halted-endpoint recovery, hot-plug and
  hub refills all happen there.
* **Binding.** A host controller driver binds only when the command line
  names it (`openrfs.drivers=ipxe-xhci,...`); under `auto` the SeaBIOS USB
  drivers own the controllers and two stacks must never share one. A
  function driver the command line does not name keeps its table entry with
  no IDs, so the USB core chooses configurations as if it were absent
  (QEMU's usb-net offers CDC-ECM and RNDIS configurations; naming one picks
  it). Registering the controller's bus enumerates every attached device
  synchronously. The layer then runs the scheduler for 1.5 s, which is when
  a full-speed device an EHCI controller hands to its companion UHCI
  controller is enumerated (the companion defers its ports until the EHCI
  bus exists), and publishes every network function found. A device
  hot-plugged later is enumerated by iPXE but not published: the kernel has
  no path for a network interface appearing after boot.
* **Glue-provided pieces.** `pci_read_config()` describes another function
  from the kernel's enumeration without claiming it (EHCI and UHCI look for
  their companions this way); `acpi_mac()` reports no system-specific MAC,
  as iPXE does on a system without the ACPI object; iPXE's unique error
  numbers collapse to their POSIX base codes, as everywhere in this port
  (no USB file compares against a derived code); `digit_value()` and
  `ssnprintf()` follow `core/string.c` and `core/vsprintf.c`.

`make IPXE_DEBUG=1` builds the iPXE layer with its `DBG()`/`DBGC()` messages
on the serial console, as an iPXE `DEBUG=` build would.

## Evidence and its limits

`tools/run_driver_tests.py` boots each supported QEMU NIC model with
`openrfs.drivers=auto` (the ISA NE2000 by name) and requires DHCP, ICMP echo
and a 256 KiB HTTP
transfer verified byte for byte, with a packet capture kept beside each serial
log. QEMU models the register interfaces of the real parts; a pass is evidence
about those models, not about physical hardware.

The USB scenarios run the same network plan through iPXE's USB stack:
CDC-ECM on QEMU's xHCI (`qemu-xhci`) and NEC uPD720200 (`nec-usb-xhci`)
models, behind a USB 1.1 hub on xHCI, on the PIIX3 UHCI model, and on the
ICH9 arrangement of an EHCI controller with three UHCI companions. QEMU's
usb-net is a full-speed device and QEMU has no high-speed network device,
so in that last scenario iPXE's EHCI driver initialises the controller,
resets the port and hands it to the companion, whose UHCI driver carries
the traffic; no transfer crosses the EHCI schedules, and the EHCI driver's
data path is not exercised by this suite. QEMU's usb-net reports its MAC to
CDC-ECM hosts with the first octet replaced by 0x40 (`usb_net_realize`),
which is the address the ECM scenarios expect.

The RNDIS scenario is an expected failure: QEMU 8.2's usb-net refuses any
RNDIS query whose empty information buffer starts at the end of the message
(`rndis_query_response()` requires `InformationBufferOffset + 8` to be less
than the message length). iPXE's `rndis.c` sends its queries that way
(offset 20, length 0); the initialise message before them completes, and
the query for the permanent MAC address is stalled. Linux's `rndis_host`
passes the same check only because its MAC query carries a 48-byte input
buffer.

QEMU 8.2's `igb` (82576) model implements only advanced receive descriptors
and raises transmit completions only through MSI-X vector routing, so it
cannot carry this driver's legacy-descriptor, polled traffic; that scenario is
reported as an expected failure with the reason. The 82576 remains in the
driver's ID table because real 82576 silicon supports legacy descriptors.
