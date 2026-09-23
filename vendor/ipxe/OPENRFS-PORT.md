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

## Evidence and its limits

`tools/run_driver_tests.py` boots each supported QEMU NIC model with
`openrfs.drivers=auto` and requires DHCP, ICMP echo and a 256 KiB HTTP
transfer verified byte for byte, with a packet capture kept beside each serial
log. QEMU models the register interfaces of the real parts; a pass is evidence
about those models, not about physical hardware.

QEMU 8.2's `igb` (82576) model implements only advanced receive descriptors
and raises transmit completions only through MSI-X vector routing, so it
cannot carry this driver's legacy-descriptor, polled traffic; that scenario is
reported as an expected failure with the reason. The 82576 remains in the
driver's ID table because real 82576 silicon supports legacy descriptors.
